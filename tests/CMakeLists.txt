add_executable(firmware_tests test_firmware.c ../decoder/decoder.c
			   ../spi_capture/spi_capture_logic.c ../controller/controller.c
			   ../scpi_control.c)
target_include_directories(firmware_tests PRIVATE mocks .. ../decoder ../spi_capture ../controller)
target_compile_options(firmware_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_library(scpi_parser STATIC)
foreach(source error fifo ieee488 minimal parser units utils lexer expression)
	target_sources(scpi_parser PRIVATE ../libs/scpi-parser/libscpi/src/${source}.c)
endforeach()
target_include_directories(scpi_parser PUBLIC ../libs/scpi-parser/libscpi/inc)
target_link_libraries(firmware_tests PRIVATE scpi_parser m)
add_test(NAME firmware_tests COMMAND firmware_tests)