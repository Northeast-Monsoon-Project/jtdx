# Include after wsjt_fort and the test infrastructure are defined.
# Keep the upstream waveform oracle separate from production symbols.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../upstream/wsjtx-3.0.2/lib/ft8/gen_ft8wave.f90" _fm_reference_wave)
string(REPLACE "gen_ft8wave" "reference_gen_ft8wave" _fm_reference_wave "${_fm_reference_wave}")
# The upstream timer import is unused; omit it so the oracle is self-contained.
string(REPLACE "  use timer_module, only: timer\n" "" _fm_reference_wave "${_fm_reference_wave}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/reference_ft8wave.f90" "${_fm_reference_wave}")
add_executable(fm_dsp_protocol_compatibility "${CMAKE_CURRENT_LIST_DIR}/../../tests/DspProtocolCompatibility.f90")
target_include_directories(fm_dsp_protocol_compatibility BEFORE PRIVATE "$<TARGET_PROPERTY:wsjt_fort,Fortran_MODULE_DIRECTORY>")
target_compile_options(fm_dsp_protocol_compatibility PRIVATE -ffree-line-length-none)
target_link_libraries(fm_dsp_protocol_compatibility PRIVATE wsjt_fort)
add_test(NAME DspProtocolCompatibility COMMAND fm_dsp_protocol_compatibility)
add_executable(fm_dsp_waveform_compatibility "${CMAKE_CURRENT_LIST_DIR}/../../tests/DspWaveformCompatibility.f90" "${CMAKE_CURRENT_BINARY_DIR}/reference_ft8wave.f90")
target_compile_options(fm_dsp_waveform_compatibility PRIVATE -ffree-line-length-none)
target_link_libraries(fm_dsp_waveform_compatibility PRIVATE wsjt_fort)
add_test(NAME DspWaveformCompatibility COMMAND fm_dsp_waveform_compatibility)
