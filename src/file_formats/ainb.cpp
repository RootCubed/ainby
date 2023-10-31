#include "ainb.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>

void AINB::Clear() {
    ainbFile = nullptr;
    ainbHeader = {};
    for (u32 i = 0; i < ValueTypeCount; i++) {
        immParams[i].clear();
        inputParams[i].clear();
        outputParams[i].clear();
    }
    preconditions.clear();
    name = "";
    fileCategory = "";
    commands.clear();
    nodes.clear();
    gparams.Clear();
    embeddedAinbs.clear();
    multiParams.clear();
}

void AINB::Read(std::istream &stream) {
    Clear();
    ainbFile = &stream;

    Read(&ainbHeader);
    if (strncmp(ainbHeader.magic, "AIB ", 4) != 0) {
        throw std::runtime_error("Invalid AINB magic");
    }
    name = ReadString(ainbHeader._name);
    fileCategory = ReadString(ainbHeader._fileCategory);

    u32 commandsOffset = ainbFile->tellg();

    // Global parameters
    ainbFile->seekg(ainbHeader.globalParamOffset);
    gparams.Read(*this);

    // Immediate params
    ainbFile->seekg(ainbHeader.immParamOffset);
    u32 immStopOffsets[ValueTypeCount];
    for (u32 i = 0; i < ValueTypeCount; i++) {
        if (i == 0) {
            ReadU32();
            continue;
        }
        immStopOffsets[i - 1] = ReadU32();
    }
    immStopOffsets[ValueTypeCount - 1] = ainbHeader.ioParamsOffset;

    for (u32 i = 0; i < ValueTypeCount; i++) {
        while (ainbFile->tellg() < immStopOffsets[i]) {
            ImmediateParam p(static_cast<ValueType>(i));
            p.Read(*this);
            immParams[i].push_back(p);
        }
    }
    assert(ainbFile->tellg() == ainbHeader.ioParamsOffset);

    // Multi-parameters (Read before I/O parameters so that they can resolve multi-params)
    ainbFile->seekg(ainbHeader.multiParamArrOffset);
    while (ainbFile->tellg() < ainbHeader.residentUpdateArrOffset) {
        MultiParam p;
        p.Read(*this);
        multiParams.push_back(p);
    }

    // I/O Parameters
    ainbFile->seekg(ainbHeader.ioParamsOffset);
    u32 ioStopOffsets[ValueTypeCount * 2];
    for (u32 i = 0; i < ValueTypeCount * 2; i++) {
        if (i == 0) {
            ReadU32();
            continue;
        }
        ioStopOffsets[i - 1] = ReadU32();
    }
    ioStopOffsets[ValueTypeCount * 2 - 1] = ainbHeader.multiParamArrOffset;

    for (u32 i = 0; i < ValueTypeCount; i++) {
        ValueType type = static_cast<ValueType>(i);
        while (ainbFile->tellg() < ioStopOffsets[i * 2]) {
            InputParam p(type);
            p.Read(*this);
            inputParams[i].push_back(p);
        }
        while (ainbFile->tellg() < ioStopOffsets[i * 2 + 1]) {
            OutputParam p(type);
            p.Read(*this);
            outputParams[i].push_back(p);
        }
    }
    assert(ainbFile->tellg() == ainbHeader.multiParamArrOffset);

    // Precondition nodes
    ainbFile->seekg(ainbHeader.preconditionNodeArrOffset);
    u32 preconditionNodesEnd = (ainbHeader.exbOffset == 0) ? ainbHeader.embeddedAinbsOffset : ainbHeader.exbOffset;
    while (ainbFile->tellg() < preconditionNodesEnd) {
        preconditions.push_back(ReadU16());
        assert(ReadU16() == 0); // Padding
    }

    // EXB section
    if (ainbHeader.exbOffset != 0) {
        assert(ainbFile->tellg() == ainbHeader.exbOffset);

        char exbMagic[4];
        ainbFile->read(exbMagic, 4);
        assert(strncmp(exbMagic, "EXB ", 4) == 0);

        ainbFile->seekg(ainbHeader.embeddedAinbsOffset);
    }

    // Embedded AINBs
    assert(ainbFile->tellg() == ainbHeader.embeddedAinbsOffset);
    u32 embAinbCount = ReadU32();
    for (u32 i = 0; i < embAinbCount; i++) {
        embeddedAinbs.push_back(ReadString(ReadU32()));
        std::string category = ReadString(ReadU32());
        u32 count = ReadU32();
        if (count > 1) {
            std::cout << "Embedded AINB " << embeddedAinbs.back() << " has count value of " << count << std::endl;
        }
    }

    assert(ainbFile->tellg() == ainbHeader.entryStringsOffset);

    // Entry strings
    u32 entryStringCount = ReadU32();
    for (u32 i = 0; i < entryStringCount; i++) {
        u32 nodeIndex = ReadU32();
        std::string str1 = ReadString(ReadU32());
        std::string str2 = ReadString(ReadU32());
    }

    assert(ainbFile->tellg() == ainbHeader.x70SectionOffset);

    // Node list
    ainbFile->seekg(commandsOffset + ainbHeader.commandCount * sizeof(Command::FileDataLayout));
    for (u32 i = 0; i < ainbHeader.nodeCount; i++) {
        Node n;
        n.Read(*this);
        nodes.push_back(n);
    }

    // Fill in connected nodes
    for (Node &n : nodes) {
        for (Param &p : n.GetParams()) {
            if (p.paramType == ParamType::Input) {
                InputParam &ip = static_cast<InputParam &>(p);
                for (int inputIdx : ip.inputNodeIdxs) {
                    n.inNodes.push_back(&nodes[inputIdx]);
                    nodes[inputIdx].outNodes.push_back(&n);
                }
            }
        }
        for (NodeLink &nl : n.nodeLinks) {
            if (nl.type == LinkType::Flow || nl.type == LinkType::ForkJoin) {
                n.outNodes.push_back(&nodes[nl.idx]);
                nodes[nl.idx].inNodes.push_back(&n);
            }
        }
    }

    // Command list
    ainbFile->seekg(commandsOffset);
    for (u32 i = 0; i < ainbHeader.commandCount; i++) {
        Command c;
        c.Read(*this);
        commands.push_back(c);
    }

    ainbFile = nullptr;
}

std::string AINB::ReadString(u32 offset) {
    size_t oldPos = ainbFile->tellg();
    if (!ainbFile->seekg(ainbHeader.stringPoolOffset + offset)) {
        throw std::runtime_error("Invalid string offset in ReadString");
    }
    std::string res;
    std::getline(*ainbFile, res, '\0');
    ainbFile->seekg(oldPos);
    return res;
}

template <typename T>
void AINB::Read(T *dest, std::streampos offset) {
    if (offset != -1) ainbFile->seekg(offset);
    ainbFile->read((char*) dest, sizeof(T));
}

template <typename T>
T AINB::Read(std::streampos offset) {
    T r;
    Read(&r, offset);
    return r;
}

AINB::ainbValue AINB::ReadAinbValue(AINB::ValueType dataType, std::streampos offset) {
    switch (dataType) {
        case ValueType::Int:
            return this->ReadU32(offset);
        case ValueType::Bool:
            return (bool) this->ReadU32(offset);
        case ValueType::Float:
            return this->ReadF32(offset);
        case ValueType::String:
            return this->ReadString(this->ReadU32(offset));
        case ValueType::Vec3f:
            return vec3f {
                .x = this->ReadF32(offset),
                .y = this->ReadF32(),
                .z = this->ReadF32()
            };
        default:
            return "";
    }
}

std::string AINB::GUID::ToString() {
    char str[37];
    snprintf(str, 37, "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
        d1, d2, d3, d4, d5[0], d5[1], d5[2], d5[3], d5[4], d5[5]);
    return str;
}

void AINB::Command::Read(AINB &ainb) {
    ainb.Read(&data);
    name = ainb.ReadString(data._name);
    rootNode = &ainb.nodes[data.leftNodeIdx];
}

void AINB::ImmediateParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    if (dataType == ValueType::UserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }
    flags = ainb.ReadU32();
    value = ainb.ReadAinbValue(dataType);
}

void AINB::InputParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    if (dataType == ValueType::UserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }

    s16 inputNodeIdx = ainb.Read<s16>();
    if (inputNodeIdx <= -100) {
        ReadMultiParam(ainb, inputNodeIdx);
    } else {
        s16 inputParamIdx = ainb.Read<s16>();
        if (inputNodeIdx != -1) {
            inputNodeIdxs.push_back(inputNodeIdx);
            inputParamIdxs.push_back(inputParamIdx);
        }
        flags = ainb.ReadU32();
        if (dataType == ValueType::UserDefined) {
            ainb.ReadU32(); // unknown
        }
    }

    defaultValue = ainb.ReadAinbValue(dataType);
}

void AINB::InputParam::ReadMultiParam(AINB &ainb, int multiParamBase) {
    u16 multiParamCount = ainb.ReadU16();
    flags = ainb.ReadU32();
    int startOffset = -multiParamBase - 100;
    for (int i = 0; i < multiParamCount; i++) {
        MultiParam &mp = ainb.multiParams[startOffset + i];
        inputNodeIdxs.push_back(mp.multiParam.nodeIdx);
        inputParamIdxs.push_back(mp.multiParam.paramIdx);
    }
}

void AINB::OutputParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32() & 0x7FFFFFFF);
    if (dataType == ValueType::UserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }
}

void AINB::Node::Read(AINB &ainb) {
    ainb.Read(&data);
    name = ainb.ReadString(data._name);
    type = data.type;
    flags = data.flags;

    if (data.attachmentCount > 0) {
        std::cout << "Node " << name << " has " << data.attachmentCount << " attachments" << std::endl;
    }

    for (int i = 0; i < data.preconditionNodeCount; i++) {
        int nodeIdx = ainb.preconditions[data.basePreconditionNode + i];
        preconditionNodes.push_back(nodeIdx);
    }

    ReadBody(ainb);
}

void AINB::Node::ReadBody(AINB &ainb) {
    std::streampos savePos = ainb.ainbFile->tellg();

    ParamMetaLayout meta;
    ainb.Read(&meta, data.paramOffset);

    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (u32 j = 0; j < meta.immediate[i].count; j++) {
            immParams[i].push_back(ainb.immParams[i][meta.immediate[i].offset + j]);
        }
        for (u32 j = 0; j < meta.inputOutput[i].inputCount; j++) {
            inputParams[i].push_back(ainb.inputParams[i][meta.inputOutput[i].inputOffset + j]);
        }
        for (u32 j = 0; j < meta.inputOutput[i].outputCount; j++) {
            outputParams[i].push_back(ainb.outputParams[i][meta.inputOutput[i].outputOffset + j]);
        }
    }

    for (u32 i = 0; i < LinkTypeCount; i++) {
        for (u32 j = 0; j < meta.link[i].count; j++) {
            u32 linkOffset = ainb.ReadU32();
            std::streampos nextLinkOffsetPos = ainb.ainbFile->tellg();
            ainb.ainbFile->seekg(linkOffset);
            NodeLink nl(static_cast<LinkType>(i));
            nl.Read(ainb, data.type);
            nodeLinks.push_back(nl);
            ainb.ainbFile->seekg(nextLinkOffsetPos);
        }
    }

    ainb.ainbFile->seekg(savePos);
}

// TODO: find a better way to do this

std::vector<std::reference_wrapper<AINB::Param>> AINB::Node::GetParams() {
    if (!isMutableParamsDirty) {
        return mutableParams;
    }
    mutableParams.clear();
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (AINB::Param &p : immParams[i]) {
            mutableParams.push_back(p);
        }
        for (AINB::Param &p : inputParams[i]) {
            mutableParams.push_back(p);
        }
        for (AINB::Param &p : outputParams[i]) {
            mutableParams.push_back(p);
        }
    }
    isMutableParamsDirty = false;
    return mutableParams;
}

std::vector<std::reference_wrapper<const AINB::Param>> AINB::Node::GetParams() const {
    if (!isConstParamsDirty) {
        return constParams;
    }
    constParams.clear();
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const AINB::Param &p : immParams[i]) {
            constParams.push_back(p);
        }
        for (const AINB::Param &p : inputParams[i]) {
            constParams.push_back(p);
        }
        for (const AINB::Param &p : outputParams[i]) {
            constParams.push_back(p);
        }
    }
    isConstParamsDirty = false;
    return constParams;
}

std::unordered_map<AINB::NodeType, std::string> nodeTypeNames = {
    {AINB::Element_S32Selector, "S32Selector" },
    {AINB::Element_F32Selector, "F32Selector" },
    {AINB::Element_BoolSelector, "BoolSelector" },
    {AINB::Element_StringSelector, "StringSelector" },
    {AINB::Element_RandomSelector, "RandomSelector" },
    {AINB::Element_Simultaneous, "Simultaneous" },
    {AINB::Element_Sequential, "Sequential" },
    {AINB::Element_Fork, "Fork" },
    {AINB::Element_Join, "Join" },
    {AINB::Element_Alert, "Alert" },
    {AINB::Element_Expression, "Expression"},
    {AINB::Element_ModuleIF_Input_S32, "ModuleIF_Input_S32" },
    {AINB::Element_ModuleIF_Input_F32, "ModuleIF_Input_F32" },
    {AINB::Element_ModuleIF_Input_Vec3f, "ModuleIF_Input_Vec3f" },
    {AINB::Element_ModuleIF_Input_String, "ModuleIF_Input_String" },
    {AINB::Element_ModuleIF_Input_Bool, "ModuleIF_Input_Bool" },
    {AINB::Element_ModuleIF_Input_Ptr, "ModuleIF_Input_Ptr" },
    {AINB::Element_ModuleIF_Output_S32, "ModuleIF_Output_S32" },
    {AINB::Element_ModuleIF_Output_F32, "ModuleIF_Output_F32" },
    {AINB::Element_ModuleIF_Output_Vec3f, "ModuleIF_Output_Vec3f" },
    {AINB::Element_ModuleIF_Output_String, "ModuleIF_Output_String" },
    {AINB::Element_ModuleIF_Output_Bool, "ModuleIF_Output_Bool" },
    {AINB::Element_ModuleIF_Output_Ptr, "ModuleIF_Output_Ptr" },
    {AINB::Element_ModuleIF_Child, "ModuleIF_Child" },
    {AINB::Element_StateEnd, "StateEnd" },
    {AINB::Element_SplitTiming, "SplitTiming" }
};

std::string AINB::Node::TypeName() const {
    if (data.type == UserDefined) {
        return name;
    }
    return nodeTypeNames[data.type];
}

void AINB::NodeLink::Read(AINB &ainb, NodeType parentNodeType) {
    idx = ainb.ReadU32();
    u32 val = ainb.ReadU32();
    switch (type) {
        case LinkType::Type0:
        case LinkType::Flow:
        case LinkType::Type4:
        case LinkType::Type5: {
            std::string label = ainb.ReadString(val);
            name = label;
            switch (parentNodeType) {
                case Element_S32Selector:
                    globalParamIdx = ainb.ReadU32();
                    value = ainb.ReadU32();
                    break;
                default:
                    break;
            }
            break;
        }
        case LinkType::ForkJoin:
            // Fork/join stuff is not clear to me yet, placeholder
            name = "Type 3 Link";
            break;
        default:
            std::cout << "Unknown link type " << static_cast<u32>(type) << std::endl;
            break;
    }
}

std::unordered_map<AINB::GlobalParamValueType, AINB::ValueType> globalTypeMap = {
    { AINB::GlobalParamValueType::String, AINB::ValueType::String },
    { AINB::GlobalParamValueType::Int, AINB::ValueType::Int },
    { AINB::GlobalParamValueType::Float, AINB::ValueType::Float },
    { AINB::GlobalParamValueType::Bool, AINB::ValueType::Bool },
    { AINB::GlobalParamValueType::Vec3f, AINB::ValueType::Vec3f },
    { AINB::GlobalParamValueType::UserDefined, AINB::ValueType::UserDefined }
};

void AINB::Gparams::Read(AINB &ainb) {
    u16 numEntries[ValueTypeCount];
    for (u32 i = 0; i < ValueTypeCount; i++) {
        numEntries[i] = ainb.ReadU16();
        ainb.ainbFile->ignore(6); // ignore
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (u16 j = 0; j < numEntries[i]; j++) {
            u32 nameOffsAndFlags = ainb.ReadU32();
            Gparam p {
                .name = ainb.ReadString(nameOffsAndFlags & 0x3FFFFF),
                .dataType = static_cast<GlobalParamValueType>(i),
                .defaultValue = ainbValue(),
                .notes = ainb.ReadString(ainb.ReadU32()),
            };
            gparams.push_back(p);
        }
    }
    for (Gparam &p : gparams) {
        if (p.dataType == GlobalParamValueType::UserDefined) {
            continue;
        }
        p.defaultValue = ainb.ReadAinbValue(globalTypeMap[p.dataType]);
    }
    for (Gparam &p : gparams) {
        p.fileRef = ainb.ReadString(ainb.ReadU32());
        u32 fileRefHash = ainb.ReadU32();
        u32 unk1 = ainb.ReadU32();
        u32 unk2 = ainb.ReadU32();
        std::cout << "Gparam " << p.name << " has file ref " << p.fileRef << ", " << std::hex << " " << fileRefHash << " " << unk1 << " " << unk2 << std::dec << std::endl;
    }
}

std::string AINB::Gparams::Gparam::TypeString() const {
    switch (dataType) {
        case GlobalParamValueType::String:
            return "string";
        case GlobalParamValueType::Int:
            return "int";
        case GlobalParamValueType::Float:
            return "float";
        case GlobalParamValueType::Bool:
            return "bool";
        case GlobalParamValueType::Vec3f:
            return "vec3f";
        case GlobalParamValueType::UserDefined:
            return "user-defined";
    }
    return "unknown";
}

void AINB::MultiParam::Read(AINB &ainb) {
    ainb.Read(&multiParam);
}

std::ostream &operator<<(std::ostream &os, const vec3f &vec) {
    os << vec.x << ", " << vec.y << ", " << vec.z;
    return os;
}

std::string AINB::AINBValueToString(ainbValue v) {
    std::stringstream ss;
    std::visit([&](const auto &elem) { ss << elem; }, v);
    return ss.str();
}
