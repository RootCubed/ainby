#include "ainb.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>

void AINB::AINB::Clear() {
    ainbFile = nullptr;
    ainbHeader = {};
    for (int i = 0; i < AINBTypeCount; i++) {
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

void AINB::AINB::Read(std::istream &stream) {
    Clear();
    ainbFile = &stream;

    ainbFile->read((char*) &ainbHeader, sizeof(AINBFileHeader));
    if (strncmp(ainbHeader.magic, "AIB ", 4) != 0) {
        throw std::runtime_error("Invalid AINB magic");
    }
    name = ReadString(ainbHeader._name);
    fileCategory = ReadString(ainbHeader._fileCategory);
    assert(ainbFile->tellg() == 0x74);

    // Command list
    for (int i = 0; i < ainbHeader.commandCount; i++) {
        Command c;
        c.Read(*this);
        commands.push_back(c);
    }

    u32 nodeListOffset = ainbFile->tellg();

    // Global parameters
    ainbFile->seekg(ainbHeader.globalParamOffset);

    gparams.Read(*this);

    // Immediate params
    ainbFile->seekg(ainbHeader.immParamOffset);
    u32 immStopOffsets[AINBTypeCount];
    for (int i = 0; i < AINBTypeCount; i++) {
        if (i == 0) {
            ReadU32();
            continue;
        }
        immStopOffsets[i - 1] = ReadU32();
    }
    immStopOffsets[AINBTypeCount - 1] = ainbHeader.ioParamsOffset;

    for (int i = 0; i < AINBTypeCount; i++) {
        while (ainbFile->tellg() < immStopOffsets[i]) {
            ImmediateParam p((ainbType) i);
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
    u32 ioStopOffsets[AINBTypeCount * 2];
    for (int i = 0; i < AINBTypeCount * 2; i++) {
        if (i == 0) {
            ReadU32();
            continue;
        }
        ioStopOffsets[i - 1] = ReadU32();
    }
    ioStopOffsets[AINBTypeCount * 2 - 1] = ainbHeader.multiParamArrOffset;

    for (int i = 0; i < AINBTypeCount; i++) {
        while (ainbFile->tellg() < ioStopOffsets[i * 2]) {
            InputParam p((ainbType) i);
            p.Read(*this);
            inputParams[i].push_back(p);
        }
        while (ainbFile->tellg() < ioStopOffsets[i * 2 + 1]) {
            OutputParam p((ainbType) i);
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
    for (int i = 0; i < embAinbCount; i++) {
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
    for (int i = 0; i < entryStringCount; i++) {
        ainbFile->ignore(8);
    }

    assert(ainbFile->tellg() == ainbHeader.x70SectionOffset);

    // Node list
    ainbFile->seekg(nodeListOffset);
    for (int i = 0; i < ainbHeader.nodeCount; i++) {
        Node n;
        n.Read(*this);
        nodes.push_back(n);
    }

    // Fill in connected nodes
    for (Node &n : nodes) {
        for (Param *p : n.params) {
            if (p->paramType == Param_Input) {
                InputParam *ip = static_cast<InputParam *>(p);
                for (int inputIdx : ip->inputNodeIdxs) {
                    n.inNodes.push_back(&nodes[inputIdx]);
                    nodes[inputIdx].outNodes.push_back(&n);
                }
            }
        }
        for (NodeLink &nl : n.nodeLinks) {
            if (nl.type == LinkFlow || nl.type == LinkForkJoin) {
                n.outNodes.push_back(&nodes[nl.idx]);
                nodes[nl.idx].inNodes.push_back(&n);
            }
        }
    }

    ainbFile = nullptr;
}

std::string AINB::AINB::ReadString(u32 offset) {
    size_t oldPos = ainbFile->tellg();
    if (!ainbFile->seekg(ainbHeader.stringPoolOffset + offset)) {
        throw std::runtime_error("Invalid string offset in ReadString");
    }
    std::string res;
    std::getline(*ainbFile, res, '\0');
    ainbFile->seekg(oldPos);
    return res;
}

float AINB::AINB::ReadF32(u32 offset) {
    if (offset != -1) ainbFile->seekg(offset);
    union {
        u32 i;
        float f;
    } r;
    ainbFile->read((char*)&r.i, 4);
    return r.f;
}

u32 AINB::AINB::ReadU32(u32 offset) {
    if (offset != -1) ainbFile->seekg(offset);
    u32 r;
    ainbFile->read((char*)&r, 4);
    return r;
}

u16 AINB::AINB::ReadU16(u32 offset) {
    if (offset != -1) ainbFile->seekg(offset);
    u16 r;
    ainbFile->read((char*)&r, 2);
    return r;
}

u8 AINB::AINB::ReadU8(u32 offset) {
    if (offset != -1) ainbFile->seekg(offset);
    u8 r;
    ainbFile->read((char*)&r, 1);
    return r;
}

std::string AINB::GUID::ToString() {
    char str[37];
    sprintf(str, "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
        d1, d2, d3, d4, d5[0], d5[1], d5[2], d5[3], d5[4], d5[5]);
    return str;
}

void AINB::Command::Read(AINB &ainb) {
    ainb.ainbFile->read((char*) &fileCommand, sizeof(AINBFileCommand));
    name = ainb.ReadString(fileCommand._name);
}

void AINB::ImmediateParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    switch (dataType) {
        case AINBInt:
            ainb.ReadU32(); // Flags
            value = ainb.ReadU32();
            break;
        case AINBBool:
            ainb.ReadU32(); // Flags
            value = (bool) ainb.ReadU32();
            break;
        case AINBFloat:
            ainb.ReadU32(); // Flags
            value = ainb.ReadF32();
            break;
        case AINBString:
            ainb.ReadU32(); // Flags
            value = ainb.ReadString(ainb.ReadU32());
            break;
        case AINBVec3f:
            ainb.ReadU32(); // Flags
            value = vec3f {
                .x = ainb.ReadF32(),
                .y = ainb.ReadF32(),
                .z = ainb.ReadF32()
            };
            break;
        case AINBUserDefined:
            value = ainb.ReadString(ainb.ReadU32());
            ainb.ReadU32(); // Flags
            break;
    }
}

void AINB::InputParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    if (dataType == AINBUserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }

    s16 inputNodeIdx = (s16) ainb.ReadU16();
    if (inputNodeIdx <= -100) {
        ReadMultiParam(ainb, inputNodeIdx);
    } else {
        s16 inputParamIdx = (s16) ainb.ReadU16();
        if (inputNodeIdx != -1) {
            inputNodeIdxs.push_back(inputNodeIdx);
            inputParamIdxs.push_back(inputParamIdx);
        }
        flags = ainb.ReadU32();
    }

    ReadDefaultValue(ainb);
}

void AINB::InputParam::ReadDefaultValue(AINB &ainb) {
    switch (dataType) {
        case AINBInt:
            defaultValue = ainb.ReadU32();
            break;
        case AINBBool:
            defaultValue = (bool) ainb.ReadU32();
            break;
        case AINBFloat:
            defaultValue = ainb.ReadF32();
            break;
        case AINBString:
            defaultValue = ainb.ReadString(ainb.ReadU32());
            break;
        case AINBVec3f:
            defaultValue = vec3f {
                .x = ainb.ReadF32(),
                .y = ainb.ReadF32(),
                .z = ainb.ReadF32()
            };
            break;
        case AINBUserDefined:
            ainb.ReadU32(); // Unknown
            break;
    }
}

void AINB::InputParam::ReadMultiParam(AINB &ainb, int multiParamBase) {
    u16 multiParamCount = ainb.ReadU16();
    u32 flags = ainb.ReadU32();
    int startOffset = -multiParamBase - 100;
    for (int i = 0; i < multiParamCount; i++) {
        MultiParam &mp = ainb.multiParams[startOffset + i];
        inputNodeIdxs.push_back(mp.multiParam.nodeIdx);
        inputParamIdxs.push_back(mp.multiParam.paramIdx);
    }
}

void AINB::OutputParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32() & 0x7FFFFFFF);
    if (dataType == AINBUserDefined) {
        userDefClassName = ainb.ReadString(ainb.ReadU32());
    }
}

std::string AINB::InputParam::TypeString() const {
    switch (dataType) {
        case AINBInt:
            return "int";
        case AINBBool:
            return "bool";
        case AINBFloat:
            return "float";
        case AINBString:
            return "string";
        case AINBVec3f:
            return "vec3f";
        case AINBUserDefined:
            return "user-defined";
    }
    return "unknown";
}

void AINB::Node::Read(AINB &ainb) {
    type = (nodeType_e) ainb.ReadU16();
    idx = ainb.ReadU16();
    attachmentCount = ainb.ReadU16();
    flags = ainb.ReadU8();

    ainb.ainbFile->ignore(1);

    name = ainb.ReadString(ainb.ReadU32());
    nameHash = ainb.ReadU32();

    ainb.ainbFile->ignore(4);

    paramOffset = ainb.ReadU32();
    exbFieldCount = ainb.ReadU16();
    exbValueSize = ainb.ReadU16();
    multiParamCount = ainb.ReadU16();

    ainb.ainbFile->ignore(2);

    u32 baseAttachmentIndex = ainb.ReadU32();

    u16 basePreconditionNode = ainb.ReadU16();
    u16 preconditionCount = ainb.ReadU16();

    for (int i = 0; i < preconditionCount; i++) {
        preconditionNodes.push_back(ainb.preconditions[basePreconditionNode + i]);
    }

    ainb.ainbFile->ignore(4);

    ainb.ainbFile->read((char *) &guid, sizeof(GUID));

    ReadParams(ainb);
}

void AINB::Node::ReadParams(AINB &ainb) {
    u32 immMetaOffset[AINBTypeCount];
    u32 immMetaCount[AINBTypeCount];
    u32 inputMetaOffset[AINBTypeCount];
    u32 inputMetaCount[AINBTypeCount];
    u32 outputMetaOffset[AINBTypeCount];
    u32 outputMetaCount[AINBTypeCount];
    u8 nodeLinkOffset[Link_Count];
    u32 nodeLinkCount[Link_Count];

    size_t savePos = ainb.ainbFile->tellg();
    ainb.ainbFile->seekg(paramOffset);

    for (int i = 0; i < AINBTypeCount; i++) {
        immMetaOffset[i] = ainb.ReadU32();
        immMetaCount[i] = ainb.ReadU32();
    }
    for (int i = 0; i < AINBTypeCount; i++) {
        inputMetaOffset[i] = ainb.ReadU32();
        inputMetaCount[i] = ainb.ReadU32();
        outputMetaOffset[i] = ainb.ReadU32();
        outputMetaCount[i] = ainb.ReadU32();
    }
    int inlineNodeCount = 0;
    for (int i = 0; i < Link_Count; i++) {
        nodeLinkCount[i] = ainb.ReadU8();
        nodeLinkOffset[i] = ainb.ReadU8();
        inlineNodeCount += nodeLinkCount[i];
    }

    for (int i = 0; i < AINBTypeCount; i++) {
        for (int j = 0; j < immMetaCount[i]; j++) {
            params.push_back(&ainb.immParams[i][immMetaOffset[i] + j]);
        }
        for (int j = 0; j < inputMetaCount[i]; j++) {
            params.push_back(&ainb.inputParams[i][inputMetaOffset[i] + j]);
        }
        for (int j = 0; j < outputMetaCount[i]; j++) {
            params.push_back(&ainb.outputParams[i][outputMetaOffset[i] + j]);
        }
    }

    std::vector<u32> nodeLinkOffsets;
    for (int i = 0; i < Link_Count; i++) {
        for (int j = 0; j < nodeLinkCount[i]; j++) {
            nodeLinkOffsets.push_back(ainb.ReadU32());
        }
    }

    int idx = 0;
    for (int i = 0; i < Link_Count; i++) {
        for (int j = 0; j < nodeLinkCount[i]; j++) {
            ainb.ainbFile->seekg(nodeLinkOffsets[idx++]);
            NodeLink nl((linkType_e) i);
            nl.Read(ainb, type);
            nodeLinks.push_back(nl);
        }
    }

    ainb.ainbFile->seekg(savePos);
}

std::unordered_map<AINB::nodeType_e, std::string> nodeTypeNames = {
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
    if (type == UserDefined) {
        return name;
    }
    return nodeTypeNames[type];
}

void AINB::NodeLink::Read(AINB &ainb, nodeType_e parentNodeType) {
    idx = ainb.ReadU32();
    switch ((linkType_e) type) {
        case Link_Type0:
        case LinkFlow:
        case Link_Type4:
        case Link_Type5: {
            u32 pos = ainb.ReadU32();
            std::string label = ainb.ReadString(pos);
            name = label;
            switch (parentNodeType) {
                case Element_S32Selector:
                    globalParamIdx = ainb.ReadU32();
                    value = ainb.ReadU32();
                    break;
            }
            break;
        }
        case LinkForkJoin:
            // Fork/join stuff is not clear to me yet, placeholder
            name = "Type 3 Link";
            break;
        default:
            std::cout << "Unknown link type " << type << std::endl;
            break;
    }
}

void AINB::Gparams::Read(AINB &ainb) {
    u16 numEntries[AINBTypeCount];
    for (int i = 0; i < AINBTypeCount; i++) {
        numEntries[i] = ainb.ReadU16();
        ainb.ainbFile->ignore(6); // ignore
    }
    for (int i = 0; i < AINBTypeCount; i++) {
        for (int j = 0; j < numEntries[i]; j++) {
            u32 nameOffsAndFlags = ainb.ReadU32();
            Gparam p {
                .name = ainb.ReadString(nameOffsAndFlags & 0x3FFFFF),
                .dataType = (ainbGlobalType) i,
                .defaultValue = ainbValue()
            };
            ainb.ReadU32(); // null string
            gparams.push_back(p);
        }
    }
    for (Gparam &p : gparams) {
        switch (p.dataType) {
            case AINBGString:
                p.defaultValue = ainb.ReadString(ainb.ReadU32());
                break;
            case AINBGInt:
                p.defaultValue = ainb.ReadU32();
                break;
            case AINBGFloat:
                p.defaultValue = ainb.ReadF32();
                break;
            case AINBGBool:
                p.defaultValue = (bool) ainb.ReadU32();
                break;
            case AINBGVec3f:
                p.defaultValue = vec3f {
                    .x = ainb.ReadF32(),
                    .y = ainb.ReadF32(),
                    .z = ainb.ReadF32()
                };
                break;
            case AINBGUserDefined:
                break;
        }
    }

}

std::string AINB::Gparams::Gparam::TypeString() const {
    switch (dataType) {
        case AINBGString:
            return "string";
        case AINBGInt:
            return "int";
        case AINBGFloat:
            return "float";
        case AINBGBool:
            return "bool";
        case AINBGVec3f:
            return "vec3f";
        case AINBGUserDefined:
            return "user-defined";
    }
    return "unknown";
}

void AINB::MultiParam::Read(AINB &ainb) {
    ainb.ainbFile->read((char *) &multiParam, sizeof(AINBFileMultiParam));
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
