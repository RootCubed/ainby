#include "ainb.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <MurmurHash3.h>

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

    size_t fileSize = ainbFile->seekg(0, std::ios::end).tellg();
    // string pool for later
    stringPool.resize(fileSize - ainbHeader.stringPoolOffset);
    ainbFile->seekg(ainbHeader.stringPoolOffset);
    ainbFile->read((char*) stringPool.data(), stringPool.size());

    // Global parameters
    ainbFile->seekg(ainbHeader.globalParamOffset);
    gparams.Read(*this);

    // Attachment params
    ainbFile->seekg(ainbHeader.attachmentParamsOffset);
    for (u32 i = 0; i < ainbHeader.attachmentParamCount; i++) {
        // TODO
    }

    // Immediate params
    assert(ainbFile->tellg() == ainbHeader.immParamOffset);
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
        EmbeddedAINB e;
        e.Read(*this);
        embeddedAinbs.push_back(e);
    }

    // Entry strings
    assert(ainbFile->tellg() == ainbHeader.entryStringsOffset);
    u32 entryStringCount = ReadU32();
    for (u32 i = 0; i < entryStringCount; i++) {
        u32 nodeIndex = ReadU32();
        std::string str1 = ReadString(ReadU32());
        std::string str2 = ReadString(ReadU32());
    }

    // 0x70 section
    assert(ainbFile->tellg() == ainbHeader.x70SectionOffset);
    x70Hash1 = ReadU32();
    x70Hash2 = ReadU32();

    // Child Replacement Table
    assert(ainbFile->tellg() == ainbHeader.childReplacementTableOffset);
    childReplacementTable.Read(*this);

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

void AINB::Write(std::ostream &stream) {
    outFile = &stream;

    // There are superfluous strings in the original string pool,
    // so let's keep those so that binary diffing is easier
    std::string curr = "";
    std::vector<std::string> strs;
    for (int i = 0; i < stringPool.size(); i++) {
        if (stringPool[i] == 0) {
            strs.push_back(curr);
            curr = "";
        } else {
            curr += stringPool[i];
        }
    }
    strs.push_back(curr);
    stringPool.clear();
    stringPoolMap.clear();
    for (const std::string &s : strs) {
        MakeString(s);
    }

    // Clear parameters
    for (u32 i = 0; i < ValueTypeCount; i++) {
        immParams[i].clear();
        inputParams[i].clear();
        outputParams[i].clear();
        currImmParamsOffsets[i] = 0;
        currInputParamsOffsets[i] = 0;
        currOutputParamsOffsets[i] = 0;
    }

    u32 ainbNameOffset = MakeString(name);
    u32 fileCategoryOffset = MakeString(fileCategory);

    u32 commandListOffset = sizeof(AINBFileHeader);
    outFile->seekp(commandListOffset);
    // Command list
    for (const Command &c : commands) {
        Command::FileDataLayout fdl {
            ._name = MakeString(c.name),
            .guid = c.data.guid,
            .leftNodeIdx = c.rootNode->Idx(),
            .rightNodeIdx = 0
        };
        WriteData(fdl);
    }

    // Node headers
    u32 nodeHeadersOffset = outFile->tellp();

    // Need to compute the node's body offset before we can write its header
    currNodeParamsOffset = nodeHeadersOffset + nodes.size() * sizeof(Node::FileDataLayout) + gparams.BinarySize();

    for (const Node &n : nodes) {
        n.WriteHeader(*this);
        currNodeParamsOffset += n.BodyBinarySize();
    }
    assert(outFile->tellp() == nodeHeadersOffset + nodes.size() * sizeof(Node::FileDataLayout));

    // Global parameters
    u32 globalParamsOffset = outFile->tellp();
    gparams.Write(*this);
    assert(outFile->tellp() == globalParamsOffset + gparams.BinarySize());

    // Node bodies
    u32 nodeBodiesOffset = outFile->tellp();
    for (const Node &n : nodes) {
        n.WriteBody(*this);
    }
    //assert(outFile->tellp() == currNodeParamsOffset);

    // Attachment Parameters
    u32 attachmentParamsOffset = outFile->tellp();
    // TODO

    // Immediate Parameters
    u32 immParamsOffset = outFile->tellp();
    u32 loc = immParamsOffset + ValueTypeCount * 4;
    for (u32 i = 0; i < ValueTypeCount; i++) {
        WriteData<u32>(loc);
        for (const ImmediateParam &p : immParams[i]) {
            loc += p.BinarySize();
        }
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const ImmediateParam &p : immParams[i]) {
            p.Write(*this);
        }
    }

    // I/O Parameters
    u32 ioParamsOffset = outFile->tellp();
    u32 ioLoc = ioParamsOffset + ValueTypeCount * 8;
    for (u32 i = 0; i < ValueTypeCount; i++) {
        WriteData<u32>(ioLoc);
        for (const InputParam &p : inputParams[i]) {
            ioLoc += p.BinarySize();
        }
        WriteData<u32>(ioLoc);
        for (const OutputParam &p : outputParams[i]) {
            ioLoc += p.BinarySize();
        }
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const InputParam &p : inputParams[i]) {
            p.Write(*this);
        }
        for (const OutputParam &p : outputParams[i]) {
            p.Write(*this);
        }
    }

    // Multi-Parameters
    u32 multiParamOffset = outFile->tellp();
    // TODO

    // Resident Update array
    u32 residentUpdateArrOffset = outFile->tellp();
    // TODO

    // 0x50 section
    u32 x50SectionOffset = outFile->tellp();
    // TODO

    // Precondition nodes
    u32 preconditionsOffset = outFile->tellp();
    // TODO

    // EXB section
    u32 exbOffset = outFile->tellp();
    // TODO
    bool hasEXB = false;

    // Embedded AINBs
    u32 embeddedAinbsOffset = outFile->tellp();
    WriteU32(embeddedAinbs.size());
    for (const EmbeddedAINB &e : embeddedAinbs) {
        e.Write(*this);
    }

    // Entry strings
    u32 entryStringsOffset = outFile->tellp();
    WriteU32(entryStrings.size());
    for (const std::string &s : entryStrings) {
        WriteU32(0); // TODO
        WriteU32(MakeString(s));
        WriteU32(MakeString(s)); // TODO
    }


    // 0x70 section
    u32 x70SectionOffset = outFile->tellp();
    WriteU32(x70Hash1);
    WriteU32(x70Hash2);

    // Child Replacement Table
    u32 childReplacementTableOffset = outFile->tellp();
    childReplacementTable.Write(*this);

    // 0x6C section
    u32 x6cSectionOffset = outFile->tellp();
    // TODO
    WriteU32(0);

    // Resolve Array
    u32 resolveArrOffset = outFile->tellp();
    // TODO
    WriteU32(0);

    // String pool
    u32 stringPoolOffset = outFile->tellp();
    outFile->write((char*) stringPool.data(), stringPool.size());

    // Finally, the header (written last because it needs to know the offsets of the other sections)
    outFile->seekp(0);
    AINBFileHeader header {
        .magic = {'A','I','B',' '},
        .version = ainbHeader.version,
        ._name = ainbNameOffset,
        .commandCount = commands.size(),
        .nodeCount = nodes.size(),
        .preconditionNodeCount = preconditions.size(),
        .attachmentParamCount = 0, // TODO
        .outputNodeCount = 0, // ?
        .globalParamOffset = globalParamsOffset,
        .stringPoolOffset = stringPoolOffset,
        .resolveArrOffset = resolveArrOffset,
        .immParamOffset = immParamsOffset,
        .residentUpdateArrOffset = residentUpdateArrOffset,
        .ioParamsOffset = ioParamsOffset,
        .multiParamArrOffset = multiParamOffset,
        .attachmentParamsOffset = attachmentParamsOffset,
        .attachmentParamIdxsOffset = attachmentParamsOffset,
        .exbOffset = hasEXB ? exbOffset : 0,
        .childReplacementTableOffset = childReplacementTableOffset,
        .preconditionNodeArrOffset = preconditionsOffset,
        .x50SectionOffset = x50SectionOffset,
        .embeddedAinbsOffset = embeddedAinbsOffset,
        ._fileCategory = fileCategoryOffset,
        .fileCategoryNum = 0,
        .entryStringsOffset = entryStringsOffset,
        .x6cSectionOffset = x6cSectionOffset,
        .x70SectionOffset = x70SectionOffset
    };
    WriteData(header);

    outFile = nullptr;
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

template <typename T>
void AINB::WriteData(const T data, std::streampos offset) {
    if (offset != -1) outFile->seekp(offset);
    outFile->write((char*) &data, sizeof(T));
}

u32 AINB::MakeString(const std::string &str) {
    if (stringPoolMap.find(str) != stringPoolMap.end()) {
        return stringPoolMap[str];
    }
    u32 offset = stringPool.size();
    stringPool.insert(stringPool.end(), str.begin(), str.end());
    stringPool.push_back('\0');
    stringPoolMap[str] = offset;
    return offset;
}

u32 AINB::HashString(const std::string &str) {
    u32 hash;
    MurmurHash3_x86_32(str.c_str(), str.size(), 0, &hash);
    return hash;
}

void AINB::WriteAinbValue(const ainbValue &value, std::streampos offset) {
    switch (static_cast<ValueType>(value.index())) {
        case ValueType::Int:
            WriteU32(std::get<u32>(value), offset);
            break;
        case ValueType::Bool:
            WriteU32(static_cast<u32>(std::get<bool>(value)), offset);
            break;
        case ValueType::Float:
            WriteF32(std::get<f32>(value), offset);
            break;
        case ValueType::String:
            WriteU32(MakeString(std::get<std::string>(value)), offset);
            break;
        case ValueType::Vec3f:
            WriteData(std::get<vec3f>(value), offset);
            break;
        default:
            break;
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

int AINBValueTypeSize(AINB::ValueType dataType) {
    switch (dataType) {
        case AINB::ValueType::String:
        case AINB::ValueType::Int:
        case AINB::ValueType::Float:
            return 4;
        case AINB::ValueType::Bool:
            return 4;
        case AINB::ValueType::Vec3f:
            return 12;
        case AINB::ValueType::UserDefined:
            return 0;
    }
    return 0;
}

void AINB::ImmediateParam::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    if (dataType == ValueType::UserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }
    flags = ainb.ReadU32();
    value = ainb.ReadAinbValue(dataType);
}

void AINB::ImmediateParam::Write(AINB &ainb) const {
    std::cout << "Writing immediate param " << name << " to " << std::hex << ainb.outFile->tellp() << std::dec << std::endl;
    ainb.WriteU32(ainb.MakeString(name));
    if (dataType == ValueType::UserDefined) {
        ainb.WriteU32(ainb.MakeString(className));
    }
    ainb.WriteU32(flags);
    ainb.WriteAinbValue(value);
}

u32 AINB::ImmediateParam::BinarySize() const {
    return 12 + AINBValueTypeSize(dataType);
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

void AINB::InputParam::Write(AINB &ainb) const {
    std::cout << "Writing input param " << name << " to " << std::hex << ainb.outFile->tellp() << std::dec << std::endl;
    ainb.WriteU32(ainb.MakeString(name));
    if (dataType == ValueType::UserDefined) {
        ainb.WriteU32(ainb.MakeString(className));
    }
    if (inputNodeIdxs.size() == 1) {
        ainb.WriteData<s16>(inputNodeIdxs[0]);
        ainb.WriteData<s16>(inputParamIdxs[0]);
    } else if (inputNodeIdxs.size() == 0) {
        ainb.WriteData<s16>(-1);
        ainb.WriteData<s16>(0);
    } else if (inputNodeIdxs.size() > 1) {
        // TODO: multi-params
    }
    ainb.WriteU32(flags);
    if (dataType == ValueType::UserDefined) {
        ainb.WriteU32(0); // unknown
    } else {
        ainb.WriteAinbValue(defaultValue);
    }
}

u32 AINB::InputParam::BinarySize() const {
    u32 addSize = (dataType == ValueType::UserDefined) ? 8 : 0;
    if (inputNodeIdxs.size() <= 1) {
        return 12 + addSize + AINBValueTypeSize(dataType);
    } else {
        return 0; // TODO
    }
}

void AINB::OutputParam::Read(AINB &ainb) {
    u32 nameAndFlags = ainb.ReadU32();
    name = ainb.ReadString(nameAndFlags & 0x7FFFFFFF);
    setPointerFlagBitZero = nameAndFlags >> 31;
    if (dataType == ValueType::UserDefined) {
        className = ainb.ReadString(ainb.ReadU32());
    }
}

void AINB::OutputParam::Write(AINB &ainb) const {
    std::cout << "Writing output param " << name << " to " << std::hex << ainb.outFile->tellp() << std::dec << std::endl;
    u32 nameOffset = ainb.MakeString(name);
    u32 nameAndFlags = nameOffset | (setPointerFlagBitZero << 31);
    ainb.WriteU32(nameAndFlags);
    if (dataType == ValueType::UserDefined) {
        ainb.WriteU32(ainb.MakeString(className));
    }
}

u32 AINB::OutputParam::BinarySize() const {
    return (dataType == ValueType::UserDefined) ? 8 : 4;
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

void AINB::Node::WriteHeader(AINB &ainb) const {
    FileDataLayout bData = {
        .type = type,
        .idx = data.idx,
        .attachmentCount = 0,
        .flags = data.flags,
        ._name = ainb.MakeString(name),
        .nameHash = HashString(name),
        .unk1 = 0,
        .paramOffset = ainb.currNodeParamsOffset,
        .exbFunctionCount = 0,
        .exbIOFieldSize = 0,
        .multiParamCount = 0,
        .baseAttachmentParamIdx = 0,
        .basePreconditionNode = 0,
        .preconditionNodeCount = 0,
        .x58Offset = 0,
        .guid =  data.guid,
    };
    ainb.WriteData(bData);
}

u32 AINB::Node::BodyBinarySize() const {
    u32 size = sizeof(ParamMetaLayout) + nodeLinks.size() * 4;
    for (const NodeLink &nl : nodeLinks) {
        size += nl.BinarySize();
    }
    return size;
}

void AINB::Node::WriteBody(AINB &ainb) const {
    std::cout << "Writing node body of " << name << " to " << std::hex << ainb.outFile->tellp() << std::dec << std::endl;
    ParamMetaLayout meta = {};
    for (const Param &p : GetParams()) {
        switch (p.paramType) {
            case ParamType::Immediate:
                ainb.immParams[static_cast<int>(p.dataType)].push_back(static_cast<const ImmediateParam &>(p));
                meta.immediate[static_cast<int>(p.dataType)].count++;
                break;
            case ParamType::Input:
                ainb.inputParams[static_cast<int>(p.dataType)].push_back(static_cast<const InputParam &>(p));
                meta.inputOutput[static_cast<int>(p.dataType)].inputCount++;
                break;
            case ParamType::Output:
                ainb.outputParams[static_cast<int>(p.dataType)].push_back(static_cast<const OutputParam &>(p));
                meta.inputOutput[static_cast<int>(p.dataType)].outputCount++;
                break;
        }
    }

    std::vector<NodeLink> links[LinkTypeCount];
    for (const NodeLink &nl : nodeLinks) {
        links[static_cast<int>(nl.type)].push_back(nl);
    }

    for (u32 i = 0; i < ValueTypeCount; i++) {
        meta.immediate[i].offset = ainb.currImmParamsOffsets[i];
        ainb.currImmParamsOffsets[i] += meta.immediate[i].count;
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        meta.inputOutput[i].inputOffset = ainb.currInputParamsOffsets[i];
        meta.inputOutput[i].outputOffset = ainb.currOutputParamsOffsets[i];
        ainb.currInputParamsOffsets[i] += meta.inputOutput[i].inputCount;
        ainb.currOutputParamsOffsets[i] += meta.inputOutput[i].outputCount;
    }
    int currPos = 0;
    for (u32 i = 0; i < LinkTypeCount; i++) {
        meta.link[i].offset = currPos;
        meta.link[i].count = links[i].size();
        currPos += links[i].size();
    }

    ainb.WriteData(meta, data.paramOffset); // TODO: recalculate this
    u32 linkDataPos = data.paramOffset + sizeof(ParamMetaLayout) + nodeLinks.size() * 4;
    // Link addresses
    for (u32 i = 0; i < LinkTypeCount; i++) {
        for (const NodeLink &nl : links[i]) {
            ainb.WriteU32(linkDataPos);
            linkDataPos += nl.BinarySize();
        }
    }
    // Link data
    for (u32 i = 0; i < LinkTypeCount; i++) {
        for (const NodeLink &nl : links[i]) {
            nl.Write(ainb);
        }
    }
    assert(ainb.outFile->tellp() == linkDataPos);
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

u32 AINB::NodeLink::BinarySize() const {
    switch (type) {
        case LinkType::Type0:
        case LinkType::Flow:
        case LinkType::Type4:
        case LinkType::Type5:
            return 8;
        default:
            return 12;
    }
}

void AINB::NodeLink::Write(AINB &ainb) const {
    std::cout << "Writing node link to " << std::hex << ainb.outFile->tellp() << std::dec << std::endl;
    ainb.WriteU32(idx);
    switch (type) {
        case LinkType::Type0:
        case LinkType::Flow:
        case LinkType::Type4:
        case LinkType::Type5:
            ainb.WriteU32(ainb.MakeString(name));
            // TODO: needs to know parent node type so e.g. Selectors know to write the condition value
            break;
        default:
            ainb.WriteU32(idx);
            ainb.WriteU32(linkValue);
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
                .hasFileRef = ((nameOffsAndFlags >> 23) & 1) == 0
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
        if (!p.hasFileRef) {
            continue;
        }
        p.fileRef = ainb.ReadString(ainb.ReadU32());
        u32 fileRefHash = ainb.ReadU32();
        p.unkHash1 = ainb.ReadU32();
        p.unkHash2 = ainb.ReadU32();
    }
}

u32 AINB::Gparams::BinarySize() const {
    u32 size = ValueTypeCount * 8;
    for (const Gparam &p : gparams) {
        size += 8; // name/flags + notes
        size += p.TypeSize();
        if (p.hasFileRef) {
            size += 16; // file ref + hash + 2 unknowns
        }
    }
    return size;
}

void AINB::Gparams::Write(AINB &ainb) const {
    std::vector<Gparam> byType[ValueTypeCount];
    for (const Gparam &p : gparams) {
        byType[static_cast<int>(p.dataType)].push_back(p);
    }

    u16 idx = 0;
    u16 offset = 0;
    for (u32 i = 0; i < ValueTypeCount; i++) {
        ainb.WriteU16(byType[i].size());
        ainb.WriteU16(idx);
        ainb.WriteU16(offset);
        ainb.WriteU16(0);
        idx += byType[i].size();
        for (const Gparam &p : byType[i]) {
            offset += p.TypeSize();
        }
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const Gparam &p : byType[i]) {
            u32 nameOffs = ainb.MakeString(p.name);
            ainb.WriteU32(nameOffs | (!p.hasFileRef << 23));
            ainb.WriteU32(ainb.MakeString(p.notes));
        }
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const Gparam &p : byType[i]) {
            if (p.dataType != GlobalParamValueType::UserDefined) {
                ainb.WriteAinbValue(p.defaultValue);
            }
        }
    }
    for (u32 i = 0; i < ValueTypeCount; i++) {
        for (const Gparam &p : byType[i]) {
            if (!p.hasFileRef) {
                continue;
            }
            ainb.WriteU32(ainb.MakeString(p.fileRef));
            ainb.WriteU32(HashString(p.fileRef));
            ainb.WriteU32(p.unkHash1);
            ainb.WriteU32(p.unkHash2);
        }
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

int AINB::Gparams::Gparam::TypeSize() const {
    switch (dataType) {
        case GlobalParamValueType::String:
        case GlobalParamValueType::Int:
        case GlobalParamValueType::Float:
            return 4;
        case GlobalParamValueType::Bool:
            return 4;
        case GlobalParamValueType::Vec3f:
            return 12;
        case GlobalParamValueType::UserDefined:
            return 0;
    }
    return 0;
}

void AINB::MultiParam::Read(AINB &ainb) {
    ainb.Read(&multiParam);
}

void AINB::EmbeddedAINB::Read(AINB &ainb) {
    name = ainb.ReadString(ainb.ReadU32());
    fileCategory = ainb.ReadString(ainb.ReadU32());
    count = ainb.ReadU32();
}

void AINB::EmbeddedAINB::Write(AINB &ainb) const {
    ainb.WriteU32(ainb.MakeString(name));
    ainb.WriteU32(ainb.MakeString(fileCategory));
    ainb.WriteU32(count);
}

void AINB::ChildReplacementTable::Read(AINB &ainb) {
    ainb.Read(&header);
    for (u32 i = 0; i < header.replacementCount; i++) {
        FileDataLayout fdl;
        ainb.Read(&fdl);
        entries.push_back(fdl);
    }
}

void AINB::ChildReplacementTable::Write(AINB &ainb) const {
    ainb.WriteData(header);
    for (const FileDataLayout &fdl : entries) {
        ainb.WriteData(fdl);
    }
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
