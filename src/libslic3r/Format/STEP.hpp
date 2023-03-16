#ifndef slic3r_Format_STEP_hpp_
#define slic3r_Format_STEP_hpp_

namespace Slic3r {

class TriangleMesh;
class ModelObject;

// load step stage
const int LOAD_STEP_STAGE_READ_FILE          = 0;
const int LOAD_STEP_STAGE_GET_SOLID          = 1;
const int LOAD_STEP_STAGE_GET_MESH           = 2;
const int LOAD_STEP_STAGE_NUM                = 3;
const int LOAD_STEP_STAGE_UNIT_NUM           = 5;

typedef std::function<void(int load_stage, int current, int total, bool& cancel)> ImportStepProgressFn;
typedef std::function<void(bool isUtf8)> StepIsUtf8Fn;

//BBS: Load an step file into a provided model.
extern bool load_step(const char *path, Model *model, bool& is_cancel, ImportStepProgressFn proFn = nullptr, StepIsUtf8Fn isUtf8Fn = nullptr);

//BBS: Used to detect what kind of encoded type is used in name field of step
// If is encoded in UTF8, the file don't need to be handled, then return the original path directly.
// If is encoded in GBK, then translate to UTF8 and generate a new temporary step file.
// If is encoded in Other type, we can't handled, then treat as UTF8. In this case, the name is garbage
// characters.
// By preprocessing, at least we can avoid garbage characters if the name field is encoded by GBK.
class StepPreProcessor {
    enum class EncodedType : unsigned char
    {
        UTF8,
        GBK,
        OTHER
    };

public:
    bool preprocess(const char* path, std::string &output_path);
    static bool isUtf8File(const char* path);
private:
    static bool isUtf8(const std::string str);
    static bool isGBK(const std::string str);
    static int preNum(const unsigned char byte);
    //BBS: default is UTF8 for most step file.
    EncodedType m_encode_type = EncodedType::UTF8;
};

}; // namespace Slic3r

#endif /* slic3r_Format_STEP_hpp_ */
