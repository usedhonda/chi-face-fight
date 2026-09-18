idf_build_get_property(component_targets __COMPONENT_TARGETS)
if ("___idf_espressif__esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
elseif("___idf_esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()
set(cmake_dir ${espdl_dir}/fbs_loader/cmake)
include(${cmake_dir}/utilities.cmake)

set(src_dirs        .)

set(include_dirs    .)

set(requires        esp-dl)

# PlatformIO exposes BUILD_DIR as a project-relative path. ESP-DL passes this
# path through a nested custom command, which otherwise resolves it twice.
get_filename_component(absolute_build_dir "${CMAKE_BINARY_DIR}" REALPATH)
set(packed_model ${absolute_build_dir}/espdl_models/human_face_detect.espdl)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

if (NOT CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD)
    if (IDF_TARGET STREQUAL "esp32s3")
        set(models_dir ${COMPONENT_DIR}/models/s3)
    elseif (IDF_TARGET STREQUAL "esp32p4" OR IDF_TARGET STREQUAL "esp32s31")
        set(models_dir ${COMPONENT_DIR}/models/p4)
    endif()

    file(MAKE_DIRECTORY ${absolute_build_dir}/espdl_models)
    set(models)
    if(CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1)
        list(APPEND models ${models_dir}/human_face_detect_msr_s8_v1.espdl
                        ${models_dir}/human_face_detect_mnp_s8_v1.espdl)
    endif()
    if(CONFIG_FLASH_ESPDET_PICO_224_224_FACE)
        list(APPEND models ${models_dir}/espdet_pico_224_224_face.espdl)
    endif()
    if(CONFIG_FLASH_ESPDET_PICO_416_416_FACE)
        list(APPEND models ${models_dir}/espdet_pico_416_416_face.espdl)
    endif()

    set(pack_model_exe ${espdl_dir}/fbs_loader/pack_espdl_models.py)
    execute_process(
        COMMAND python ${pack_model_exe} --model_path ${models} --out_file ${packed_model}
        COMMAND_ERROR_IS_FATAL ANY)

    if(CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA)
        set(embed_source ${absolute_build_dir}/human_face_detect.espdl.S)
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                    -D DATA_FILE=${packed_model}
                    -D SOURCE_FILE=${embed_source}
                    -D FILE_TYPE=BINARY
                    -P ${cmake_dir}/data_file_embed_asm_aligned.cmake
            COMMAND_ERROR_IS_FATAL ANY)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} APPEND PROPERTY
                     ADDITIONAL_CLEAN_FILES ${embed_source})
        target_sources(${COMPONENT_LIB} PRIVATE ${embed_source})
    endif()

    if(CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_PARTITION)
        add_custom_target(human_face_detect_model ALL DEPENDS ${packed_model})
        add_dependencies(flash human_face_detect_model)
        esptool_py_flash_to_partition(flash "human_face_det" ${packed_model})
    endif()
endif()
