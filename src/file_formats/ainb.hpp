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
    using ainbValue = std::variant<u32, bool, f32, std::string, vec3f>; // Same order as ValueType enum
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

    enum class ValueType {
        Int,
        Bool,
        Float,
        String,
        Vec3f,
        UserDefined,
        _Count
    };
    enum class GlobalParamValueType {
        String,
        Int,
        Float,
        Bool,
        Vec3f,
        UserDefined
    };
    static const u32 ValueTypeCount = static_cast<u32>(ValueType::_Count);

    enum class ParamType {
        Immediate,
        Input,
        Output
    };

    enum class LinkType {
        Type0,
        Type1,
        Flow,
        ForkJoin,
        Type4,
        Type5,
        Type6,
        Type7,
        Type8,
        Type9,
        _Count
    };
    static const u32 LinkTypeCount = static_cast<u32>(LinkType::_Count);

    enum NodeType : u16 {
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

    class Param {
        virtual void Read(AINB &ainb) = 0;
    protected:
        Param(ParamType paramType, ValueType dataType) :
            paramType(paramType), dataType(dataType) {}
        virtual ~Param() {}
    public:
        ParamType paramType;
        ValueType dataType;
        std::string name = "";
        std::string className = ""; // Only for NodeType::UserDefined

        u32 flags = 0;

        friend class AINB;
    };

    class ImmediateParam : public Param {
        void Read(AINB &ainb);
    public:
        ImmediateParam(ValueType type) : Param(ParamType::Immediate, type) {}
        ainbValue value;

        friend class AINB;
    };

    class InputParam : public Param {
    private:
        void Read(AINB &ainb);
        void ReadMultiParam(AINB &ainb, int multiParamBase);
    public:
        InputParam(ValueType type) : Param(ParamType::Input, type) {}

        std::vector<int> inputNodeIdxs;
        std::vector<int> inputParamIdxs;
        u32 flags;

        ainbValue defaultValue;

        friend class AINB;
    };

    class OutputParam : public Param {
        void Read(AINB &ainb);
    public:
        OutputParam(ValueType type) : Param(ParamType::Output, type) {}

        friend class AINB;
    };

    class NodeLink {
        void Read(AINB &ainb, NodeType parentNodeType);
    public:
        NodeLink(LinkType type) : type(type) {}

        LinkType type;
        u32 idx;
        std::string name;
        u32 globalParamIdx;
        ainbValue value;

        friend class AINB;
    };

    class Node {
    private:
        void Read(AINB &ainb);
        void ReadBody(AINB &ainb);

        struct FileDataLayout {
            NodeType type;
            u16 idx;
            u16 attachmentCount;
            u8 flags;
            u8 __pad_1;
            u32 _name;
            u32 nameHash;
            u32 unk1;
            u32 paramOffset;
            u16 exbFunctionCount;
            u16 exbIOFieldSize;
            u16 multiParamCount;
            u16 __pad_2;
            u32 baseAttachmentParamIdx;
            u16 basePreconditionNode;
            u16 preconditionNodeCount;
            u16 x58Offset;
            u16 __pad_3;
            GUID guid;
        };

        FileDataLayout data;

        struct ParamMetaLayout {
            struct {
                u32 offset;
                u32 count;
            } immediate[ValueTypeCount];
            struct {
                u32 inputOffset;
                u32 inputCount;
                u32 outputOffset;
                u32 outputCount;
            } inputOutput[ValueTypeCount];
            struct {
                u8 count;
                u8 offset;
            } link[LinkTypeCount];
        };

        std::vector<const Node *> inNodes;
        std::vector<const Node *> outNodes;

    public:
        std::string TypeName() const;

        u16 Idx() const { return data.idx; }
        const std::vector<const Node *> &GetInNodes() const { return inNodes; }
        const std::vector<const Node *> &GetOutNodes() const { return outNodes; }

        std::vector<ImmediateParam> immParams;
        std::vector<InputParam> inputParams;
        std::vector<OutputParam> outputParams;
        std::vector<std::reference_wrapper<Param>> GetParams();
        std::vector<std::reference_wrapper<const Param>> GetParams() const;

        std::string name; // Empty string if type != UserDefined
        NodeType type;
        u32 flags;

        std::vector<NodeLink> nodeLinks;
        std::vector<u32> preconditionNodes;

        friend class AINB;
    };

    class Command {
        void Read(AINB &ainb);

        struct FileDataLayout {
            u32 _name;
            GUID guid;
            u16 leftNodeIdx;
            u16 rightNodeIdx;
        };
        FileDataLayout data;
    public:
        std::string name;
        Node *rootNode;

        friend class AINB;
    };

    class Gparams {
        void Read(AINB &ainb);
    public:
        struct Gparam {
            std::string name;
            GlobalParamValueType dataType;
            ainbValue defaultValue;
            std::string notes;

            std::string TypeString() const;
        };
        void Clear() { gparams.clear(); }

        std::vector<Gparam> gparams;

        friend class AINB;
    };

    class MultiParam {
        void Read(AINB &ainb);
    public:
        struct FileDataLayout {
            u16 nodeIdx;
            u16 paramIdx;
            u32 flags;
        };
        FileDataLayout multiParam;

        friend class AINB;
    };

private:
    // Temporary variables used during reading/writing
    std::istream *ainbFile;
    std::vector<MultiParam> multiParams;
    std::vector<u16> preconditions;
    std::vector<ImmediateParam> immParams[ValueTypeCount];
    std::vector<InputParam> inputParams[ValueTypeCount];
    std::vector<OutputParam> outputParams[ValueTypeCount];

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
    AINBFileHeader ainbHeader;

    template <typename T>
    T Read(std::streampos offset = -1);

    template <typename T>
    void Read(T *dataHolder, std::streampos offset = -1);

    std::string ReadString(u32 offset);
    ainbValue ReadAinbValue(ValueType dataType, std::streampos offset = -1);
    // Just the most common ones, use Read<T> for others
    u32 ReadU32(std::streampos offset = -1) { return Read<u32>(offset); }
    u16 ReadU16(std::streampos offset = -1) { return Read<u16>(offset); }
    f32 ReadF32(std::streampos offset = -1) { return Read<f32>(offset); }
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
