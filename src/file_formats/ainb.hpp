#pragma once

#include <ostream>
#include <variant>
#include <vector>

#include "types.h"

struct vec3f {
    f32 x, y, z;
};

std::ostream &operator<<(std::ostream &os, const vec3f &vec);

class AINB {
public:
    using ainbValue = std::variant<u32, bool, f32, std::string, vec3f>;
    static std::string AINBValueToString(ainbValue v);

    struct GUID {
        u32 d1;
        u16 d2;
        u16 d3;
        u16 d4;
        u8 d5[6];

        std::string ToString();

        std::ostream &operator<<(std::ostream &os) {
            os << ToString();
            return os;
        }
    };

    enum ainbType {
        AINBInt,
        AINBBool,
        AINBFloat,
        AINBString,
        AINBVec3f,
        AINBUserDefined,
        AINBTypeCount
    };

    enum ainbGlobalType {
        AINBGString,
        AINBGInt,
        AINBGFloat,
        AINBGBool,
        AINBGVec3f,
        AINBGUserDefined,
        AINBGTypeCount
    };

    enum paramType_e {
        Param_Imm,
        Param_Input,
        Param_Output
    };

    enum linkType_e {
        Link_Type0,
        Link_Type1,
        LinkFlow,
        LinkForkJoin,
        Link_Type4,
        Link_Type5,
        Link_Type6,
        Link_Type7,
        Link_Type8,
        Link_Type9,
        Link_Count
    };

    enum nodeType_e {
        UserDefined                    = 0,
        Element_S32Selector            = 1,
        Element_Sequential             = 2,
        Element_Simultaneous           = 3,
        Element_F32Selector            = 4,
        Element_StringSelector         = 5,
        Element_RandomSelector         = 6,
        Element_BoolSelector           = 7,
        Element_Fork                   = 8,
        Element_Join                   = 9,
        Element_Alert                  = 10,
        Element_Expression             = 20,
        Element_ModuleIF_Input_S32     = 100,
        Element_ModuleIF_Input_F32     = 101,
        Element_ModuleIF_Input_Vec3f   = 102,
        Element_ModuleIF_Input_String  = 103,
        Element_ModuleIF_Input_Bool    = 104,
        Element_ModuleIF_Input_Ptr     = 105,
        Element_ModuleIF_Output_S32    = 200,
        Element_ModuleIF_Output_F32    = 201,
        Element_ModuleIF_Output_Vec3f  = 202,
        Element_ModuleIF_Output_String = 203,
        Element_ModuleIF_Output_Bool   = 204,
        Element_ModuleIF_Output_Ptr    = 205,
        Element_ModuleIF_Child         = 300,
        Element_StateEnd               = 400,
        Element_SplitTiming            = 500
    };

    // Note: For structs directly corresponding to the file's binary data,
    // underscore-prefixed names are string table offsets

    struct AINBFileHeader {
        char magic[4];
        u32 version;
        u32 _name;
        u32 commandCount;
        u32 nodeCount;
        u32 preconditionNodeCount;
        u32 attachmentParamCount;
        u32 outputNodeCount;
        u32 globalParamOffset;
        u32 stringPoolOffset;
        u32 resolveArrOffset;
        u32 immParamOffset;
        u32 residentUpdateArrOffset;
        u32 ioParamsOffset;
        u32 multiParamArrOffset;
        u32 attachmentParamsOffset;
        u32 attachmentParamIdxsOffset;
        u32 exbOffset;
        u32 childReplacementTableOffset;
        u32 preconditionNodeArrOffset;
        u32 unk;
        u32 unk2;
        u32 unk3;
        u32 embeddedAinbsOffset;
        u32 _fileCategory;
        u32 fileCategoryNum;
        u32 entryStringsOffset;
        u32 unk4;
        u32 x70SectionOffset;
    };

    struct AINBFileCommand {
        u32 _name;
        GUID guid;
        u16 leftNodeIdx;
        u16 rightNodeIdx;
    };

    struct AINBFileMultiParam {
        u16 nodeIdx;
        u16 paramIdx;
        u32 flags;
    };

    class Command {
        void Read(AINB &ainb);
    public:
        AINBFileCommand fileCommand;
        std::string name;

        friend class AINB;
    };

    class Param {
        virtual void Read(AINB &ainb) = 0;
    protected:
        Param(paramType_e type) : paramType(type), name("") {}
        virtual ~Param() {}
    public:
        paramType_e paramType;
        std::string name;

        friend class AINB;
    };

    class ImmediateParam : public Param {
        void Read(AINB &ainb);
    public:
        ImmediateParam(ainbType type) : Param(Param_Imm), dataType(type) {}
        ainbType dataType;
        ainbValue value;

        friend class AINB;
    };

    class InputParam : public Param {
    private:
        void Read(AINB &ainb);
        void ReadDefaultValue(AINB &ainb);
        void ReadMultiParam(AINB &ainb, int multiParamBase);
    public:
        InputParam(ainbType type) : Param(Param_Input), dataType(type) {}
        std::string TypeString() const;
        ainbType dataType;

        std::vector<int> inputNodeIdxs;
        std::vector<int> inputParamIdxs;
        u32 flags;

        std::string className; // Only in UserDefined

        ainbValue defaultValue;

        friend class AINB;
    };

    class OutputParam : public Param {
        void Read(AINB &ainb);
    public:
        OutputParam(ainbType type) : Param(Param_Output), dataType(type) {}
        ainbType dataType;

        std::string userDefClassName;

        friend class AINB;
    };

    class NodeLink {
        void Read(AINB &ainb, nodeType_e parentNodeType);
    public:
        NodeLink(linkType_e type) : type(type) {}

        linkType_e type;
        u32 idx;
        std::string name;
        u32 globalParamIdx;
        ainbValue value;

        friend class AINB;
    };

    class Node {
    private:
        u16 idx;
        std::vector<const Node *> inNodes;
        std::vector<const Node *> outNodes;

        void Read(AINB &ainb);
        void ReadParams(AINB &ainb);
    public:
        std::string TypeName() const;

        u16 Idx() const { return idx; }
        const std::vector<const Node *> &GetInNodes() const { return inNodes; }
        const std::vector<const Node *> &GetOutNodes() const { return outNodes; }

        std::string name; // Empty string if type != UserDefined
        nodeType_e type;
        u16 attachmentCount;
        u8 flags;
        u32 nameHash;
        GUID guid;
        u32 paramOffset;
        u16 multiParamCount;

        u16 exbFieldCount;
        u16 exbValueSize;

        std::vector<Param *> params;
        std::vector<NodeLink> nodeLinks;
        std::vector<u32> preconditionNodes;

        friend class AINB;
    };

    class Gparams {
        void Read(AINB &ainb);
    public:
        struct Gparam {
            std::string name;
            ainbGlobalType dataType;
            ainbValue defaultValue;

            std::string TypeString() const;
        };
        void Clear() { gparams.clear(); }

        std::vector<Gparam> gparams;

        friend class AINB;
    };

    class MultiParam {
        void Read(AINB &ainb);
    public:

        AINBFileMultiParam multiParam;

        friend class AINB;
    };

private:
    std::istream *ainbFile;

    AINBFileHeader ainbHeader;
    std::vector<ImmediateParam> immParams[AINBTypeCount];
    std::vector<InputParam> inputParams[AINBTypeCount];
    std::vector<OutputParam> outputParams[AINBTypeCount];
    std::vector<MultiParam> multiParams;
    std::vector<u16> preconditions;

    std::string ReadString(u32 offset);
    float ReadF32(u32 offset = -1);
    u32 ReadU32(u32 offset = -1);
    u16 ReadU16(u32 offset = -1);
    u8 ReadU8(u32 offset = -1);
public:
    void Read(std::istream &stream);
    void Clear();

    std::string name;
    std::string fileCategory;

    std::vector<Command> commands;
    std::vector<Node> nodes;
    Gparams gparams;
    std::vector<std::string> embeddedAinbs;
};
