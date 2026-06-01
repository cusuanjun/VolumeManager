#include "error_codes.h"

namespace volumemanager {

const char* GetErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:
            return "操作成功";
        case ErrorCode::FILE_NOT_FOUND:
            return "文件未找到";
        case ErrorCode::INVALID_PATH:
            return "无效路径";
        case ErrorCode::VOLUME_FULL:
            return "卷镜像已满";
        case ErrorCode::VOLUME_NOT_FOUND:
            return "卷镜像未找到";
        case ErrorCode::INVALID_VOLUME_FORMAT:
            return "无效的卷镜像格式";
        case ErrorCode::INODE_NOT_FOUND:
            return "inode未找到";
        case ErrorCode::SERIALIZATION_ERROR:
            return "序列化/反序列化错误";
        case ErrorCode::IO_ERROR:
            return "IO错误";
        case ErrorCode::OUT_OF_MEMORY:
            return "内存不足";
        case ErrorCode::INVALID_VOLUME_ID:
            return "无效的卷镜像ID";
        case ErrorCode::INVALID_PARAMETER:
            return "无效参数";
        default:
            return "未知错误";
    }
}

} // namespace volumemanager
