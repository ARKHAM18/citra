add_library(network STATIC
    message.cpp
    message.h
    room.cpp
    room.h
    room_member.cpp
    room_member.h
)

create_target_directory_groups(network)

target_include_directories(network PRIVATE ${PROJECT_SOURCE_DIR}/externals/websocketpp)

target_link_libraries(network PRIVATE common websocketpp json-headers httplib)
