cmake_minimum_required(VERSION 3.13)

project(sdlpal_arm7 LANGUAGES C ASM)

add_executable(${PROJECT_NAME})

dkp_target_generate_symbol_list(${PROJECT_NAME})

target_compile_options(${PROJECT_NAME} PRIVATE
	-Wall
	-Wextra
)

target_sources(${PROJECT_NAME} PRIVATE
	arm7_main.c
	nds_retail_startup.c
	nds_retail_backup.s
)

set_source_files_properties(nds_retail_startup.c PROPERTIES
	COMPILE_OPTIONS "-fno-builtin"
)
