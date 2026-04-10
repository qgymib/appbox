# bin2h.cmake
# 将二进制文件转换为 C 头文件中的字节数组
# 用法: cmake -DINPUT_FILE=<> -DOUTPUT_FILE=<> -DARRAY_NAME=<> -P bin2h.cmake

cmake_minimum_required(VERSION 3.14)

if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED ARRAY_NAME)
    message(FATAL_ERROR
            "Usage: cmake -DINPUT_FILE=<file> -DOUTPUT_FILE=<file> -DARRAY_NAME=<name> -P bin2h.cmake")
endif()

file(READ "${INPUT_FILE}" hex_content HEX)
file(SIZE "${INPUT_FILE}" file_size)

# 将连续的十六进制字节 "4d5a90" 转为 "0x4d,0x5a,0x90," 格式
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," formatted "${hex_content}")

file(WRITE "${OUTPUT_FILE}"
        "/* Auto-generated from: ${INPUT_FILE} — DO NOT EDIT */\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "const uint8_t ${ARRAY_NAME}_data[] = {\n"
        "    ${formatted}\n"
        "};\n\n"
        "const size_t ${ARRAY_NAME}_size = ${file_size};\n"
)

message(STATUS "bin2h: ${INPUT_FILE} (${file_size} bytes) -> ${OUTPUT_FILE}")
