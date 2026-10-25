# Source from host checks after setting shared and changing to repository root.
: "${IDF_PATH:?Source the pinned ESP-IDF 5.5.5 export.sh first}"
export C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}$PWD/$shared/media_library/include:$PWD/$shared/device_input/include:$IDF_PATH/components/json/cJSON"
media_objects=$(mktemp -d /tmp/pearl-media-host.XXXXXX)
trap 'rm -rf "$media_objects"' EXIT HUP INT TERM
cc ${PEARL_MEDIA_SANITIZERS:--fsanitize=address,undefined} -D_POSIX_C_SOURCE=200809L -I main -c main/media.c -o "$media_objects/adapter.o"
cc ${PEARL_MEDIA_SANITIZERS:--fsanitize=address,undefined} -D_POSIX_C_SOURCE=200809L -c "$shared/media_library/media_library.c" -o "$media_objects/model.o"
cc ${PEARL_MEDIA_SANITIZERS:--fsanitize=address,undefined} -Wno-deprecated-declarations -c "$IDF_PATH/components/json/cJSON/cJSON.c" -o "$media_objects/json.o"
media_link="$media_objects/adapter.o $media_objects/model.o $media_objects/json.o"
