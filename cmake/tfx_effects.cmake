# Effects describe themselves in effects/<name>/effect.cmake, so an effect is
# one folder: code, spec, tests, tour and goldens. Read by the desktop build
# and by the firmware build (hosts/daisy), which share nothing else.
#
# effect.cmake sets, with paths relative to its own folder:
#   FX_CLASS      the tfx::Effect subclass, e.g. tfx::fx::OnceMore
#   FX_HEADER     its header, relative to effects/, e.g. oncemore/oncemore.h
#   FX_SOURCES    .cpp files
#   FX_TESTS      doctest files (optional)
#   FX_TOUR_ARGS  fx-run arguments for tests/tour.yaml (optional; with
#                 tests/golden/tour.wav present the tour is also a golden test)
#   FX_TITLE      display name, FX_SUMMARY one sentence: for the public
#                 release (publish/package.cmake)

# Every folder under effects/ that has an effect.cmake, sorted by name.
function(tfx_find_effects root out)
  file(GLOB manifests RELATIVE ${root}/effects ${root}/effects/*/effect.cmake)
  set(names "")
  foreach(m IN LISTS manifests)
    get_filename_component(dir ${m} DIRECTORY)
    list(APPEND names ${dir})
  endforeach()
  list(SORT names)
  set(${out} ${names} PARENT_SCOPE)
endfunction()

# Reads effects/<name>/effect.cmake into <name>_CLASS, <name>_HEADER,
# <name>_SOURCES, <name>_TESTS (absolute paths), <name>_TOUR_ARGS, <name>_TITLE,
# <name>_SUMMARY, <name>_DIR.
function(tfx_load_effect root name)
  set(dir ${root}/effects/${name})
  if(NOT EXISTS ${dir}/effect.cmake)
    message(FATAL_ERROR "effect '${name}': ${dir}/effect.cmake not found; "
                        "every name in TFX_EFFECTS needs a folder under effects/ with one")
  endif()
  foreach(v CLASS HEADER SOURCES TESTS TOUR_ARGS TITLE SUMMARY)
    unset(FX_${v})
  endforeach()
  include(${dir}/effect.cmake)
  foreach(v CLASS HEADER SOURCES)
    if(NOT FX_${v})
      message(FATAL_ERROR "effect '${name}': ${dir}/effect.cmake must set FX_${v}")
    endif()
  endforeach()
  set(sources "")
  foreach(s IN LISTS FX_SOURCES)
    list(APPEND sources ${dir}/${s})
  endforeach()
  set(tests "")
  foreach(t IN LISTS FX_TESTS)
    list(APPEND tests ${dir}/${t})
  endforeach()
  set(${name}_DIR ${dir} PARENT_SCOPE)
  set(${name}_CLASS ${FX_CLASS} PARENT_SCOPE)
  set(${name}_HEADER ${FX_HEADER} PARENT_SCOPE)
  set(${name}_SOURCES ${sources} PARENT_SCOPE)
  set(${name}_TESTS ${tests} PARENT_SCOPE)
  set(${name}_TOUR_ARGS ${FX_TOUR_ARGS} PARENT_SCOPE)
  set(${name}_TITLE "${FX_TITLE}" PARENT_SCOPE)
  set(${name}_SUMMARY "${FX_SUMMARY}" PARENT_SCOPE)
endfunction()
