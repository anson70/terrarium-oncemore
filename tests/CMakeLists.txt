add_executable(tfx_tests
  test_main.cpp
  test_param.cpp
  test_gesture.cpp
  test_panel.cpp
  test_script.cpp
  test_engine.cpp
  test_registry.cpp
  ${TFX_EFFECT_TESTS}) # each effect's own, from its effect.cmake
target_include_directories(tfx_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_link_libraries(tfx_tests PRIVATE tfx_offline tfx_effects tfx_alloc_guard)
if(TARGET tfx_live) # not in the per-effect source exports
  target_sources(tfx_tests PRIVATE test_tuner.cpp test_live.cpp)
  target_link_libraries(tfx_tests PRIVATE tfx_live)
endif()
tfx_warnings(tfx_tests)
add_test(NAME unit COMMAND tfx_tests)

# Command-line checks of fx-run itself: exit codes and reproducibility.
set(FXRUN $<TARGET_FILE:fx-run>)
set(OUT ${CMAKE_CURRENT_BINARY_DIR}/cli)
file(MAKE_DIRECTORY ${OUT})

add_test(NAME cli_list COMMAND ${FXRUN} --list)
set_tests_properties(cli_list PROPERTIES PASS_REGULAR_EXPRESSION "passthrough")

add_test(NAME cli_unknown_effect COMMAND ${FXRUN} nope --signal impulse)
set_tests_properties(cli_unknown_effect PROPERTIES WILL_FAIL TRUE)

# Passthrough at unity reproduces its input bit for bit, with random block sizes.
add_test(NAME cli_passthrough_source
  COMMAND ${FXRUN} passthrough --signal noise:-6 --duration 1 --out ${OUT}/noise.wav
          --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/unity.yaml)
add_test(NAME cli_passthrough_exact
  COMMAND ${FXRUN} passthrough --in ${OUT}/noise.wav --block 1-512 --seed 7
          --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/unity.yaml
          --compare ${OUT}/noise.wav --exact)
set_tests_properties(cli_passthrough_exact PROPERTIES DEPENDS cli_passthrough_source)

# Two runs with the same seed give identical audio and identical reports.
foreach(run a b)
  add_test(NAME cli_repro_${run}
    COMMAND ${FXRUN} passthrough --signal sweep:40:8000 --duration 3 --block 1-256 --seed 42
            --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gestures.yaml
            --out ${OUT}/repro_${run}.wav --report ${OUT}/repro_${run}.json --no-timing)
endforeach()
add_test(NAME cli_repro_audio COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT}/repro_a.wav ${OUT}/repro_b.wav)
add_test(NAME cli_repro_report COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT}/repro_a.json ${OUT}/repro_b.json)
set_tests_properties(cli_repro_audio cli_repro_report PROPERTIES DEPENDS "cli_repro_a;cli_repro_b")

# A failing comparison exits non-zero.
add_test(NAME cli_compare_fails
  COMMAND ${FXRUN} passthrough --in ${OUT}/noise.wav --compare ${OUT}/repro_a.wav --exact)
set_tests_properties(cli_compare_fails PROPERTIES WILL_FAIL TRUE DEPENDS "cli_passthrough_source;cli_repro_a")

# Each effect's tour (effects/<fx>/tests/tour.yaml) through every control and
# gesture at awkward rates and block sizes: exits 3 if process() allocates or
# the output has NaN/Inf.
#
# Approved sounds (effects/<fx>/tests/golden/README.md): the tour at 48 kHz
# must be bit-identical to tests/golden/tour.wav. Linux only, where the maths
# library is the one the goldens were made with.
foreach(fx IN LISTS TFX_EFFECTS)
  set(tour ${${fx}_DIR}/tests/tour.yaml)
  if(NOT EXISTS ${tour} OR NOT ${fx}_TOUR_ARGS)
    continue()
  endif()
  foreach(sr 44100 96000)
    add_test(NAME cli_${fx}_tour_${sr}
      COMMAND ${FXRUN} ${fx} ${${fx}_TOUR_ARGS} --sr ${sr} --block 1-512 --script ${tour}
              --out ${OUT}/${fx}_tour_${sr}.wav)
  endforeach()
  set(golden ${${fx}_DIR}/tests/golden/tour.wav)
  if(EXISTS ${golden} AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME golden_${fx}
      COMMAND ${FXRUN} ${fx} ${${fx}_TOUR_ARGS} --script ${tour} --compare ${golden} --exact)
  endif()
endforeach()

# fx-live without a window or audio hardware: device -> sink -> engine on
# miniaudio's null backend, checking real-time safety along the way.
if(TARGET fx-live)
  add_test(NAME live_selftest COMMAND fx-live --selftest 1)
endif()
