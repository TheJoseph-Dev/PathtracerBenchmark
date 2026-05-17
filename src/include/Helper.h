#ifndef HELPER_H
#define HELPER_H
inline static constexpr uint32_t PING_PONG_FRAMES = 2;
//inline const std::string RESOURCE_PATH_PREFIX = "src\\resources\\";
#define RESOURCE(filepath) (std::string(RESOURCE_PATH_PREFIX) + (filepath))
#endif