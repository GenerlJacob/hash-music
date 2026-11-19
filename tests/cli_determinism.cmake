# Determinism check for the command-line tool: running hashmusic twice on the same input must write
# byte-identical .mid files, and a one-character change must write a different file.
# Run by CTest as: cmake -DEXE=<hashmusic> -DWORK_DIR=<dir> -P cli_determinism.cmake
file(MAKE_DIRECTORY "${WORK_DIR}")

function(make_song text out)
  execute_process(COMMAND "${EXE}" ${ARGN} -q -o "${out}" -- "${text}"
                  RESULT_VARIABLE result OUTPUT_QUIET)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "hashmusic exited with ${result} for input '${text}'")
  endif()
endfunction()

function(files_match a b out_var)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}" "${b}" RESULT_VARIABLE differ)
  if(differ EQUAL 0)
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

foreach(mode core band)
  set(flags "")
  if(mode STREQUAL "band")
    set(flags --band)
  endif()
  make_song("jacob" "${WORK_DIR}/${mode}_jacob_run1.mid" ${flags})
  make_song("jacob" "${WORK_DIR}/${mode}_jacob_run2.mid" ${flags})
  make_song("jacob1" "${WORK_DIR}/${mode}_jacob1.mid" ${flags})

  files_match("${WORK_DIR}/${mode}_jacob_run1.mid" "${WORK_DIR}/${mode}_jacob_run2.mid" same)
  if(NOT same)
    message(FATAL_ERROR "${mode}: the same input produced two different .mid files")
  endif()
  files_match("${WORK_DIR}/${mode}_jacob_run1.mid" "${WORK_DIR}/${mode}_jacob1.mid" same)
  if(same)
    message(FATAL_ERROR "${mode}: 'jacob' and 'jacob1' produced identical .mid files")
  endif()
endforeach()

message(STATUS "Same input -> byte-identical .mid; one-character change -> different .mid (core and --band)")
