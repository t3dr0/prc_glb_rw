# Fail unless FILE contains the text TEXT (binary files included: a .glb's
# JSON chunk is plain text).

file(READ "${FILE}" content HEX)
string(HEX "${TEXT}" text_hex)
string(FIND "${content}" "${text_hex}" at)

if(at EQUAL -1)
    message(FATAL_ERROR "${FILE} does not contain \"${TEXT}\"")
endif()
