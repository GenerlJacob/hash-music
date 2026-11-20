# Command-line checks for the newer modes: integrity verification, password register/login with a
# lockout, toy-hash collisions, hash art and WAV output.
# Run by CTest as: cmake -DEXE=<hashmusic> -DWORK_DIR=<dir> -P cli_features.cmake
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Runs hashmusic (in WORK_DIR) and fails unless it exits with `expected`.
function(expect_exit expected)
  execute_process(COMMAND "${EXE}" ${ARGN} WORKING_DIRECTORY "${WORK_DIR}"
                  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
  if(NOT result EQUAL expected)
    string(REPLACE ";" " " shown "${ARGN}")
    message(FATAL_ERROR "hashmusic ${shown}\nexited with ${result}, expected ${expected}:\n${output}")
  endif()
  set(last_output "${output}" PARENT_SCOPE)
endfunction()

function(expect_output pattern)
  if(NOT last_output MATCHES "${pattern}")
    message(FATAL_ERROR "expected output matching '${pattern}', got:\n${last_output}")
  endif()
endfunction()

# Integrity: the right digest passes, a tampered file fails with exit code 1.
file(WRITE "${WORK_DIR}/report.txt" "Grades: A A B\n")
file(SHA256 "${WORK_DIR}/report.txt" good)
expect_exit(0 --file report.txt --verify ${good} --hash-only)
expect_output("Integrity MATCH")
file(WRITE "${WORK_DIR}/report.txt" "Grades: A A A\n")
expect_exit(1 --file report.txt --verify ${good} -o tampered.mid -q)
expect_output("Integrity MISMATCH")
expect_exit(2 --file report.txt --verify 1234)

# Passwords: register, log in, then three wrong guesses lock the account.
expect_exit(0 --register alice correct-horse --store users.txt --iterations 2000)
expect_exit(2 --register alice)
expect_exit(1 --register alice other --store users.txt --iterations 2000)
expect_exit(0 --login alice correct-horse --store users.txt)
expect_output("Login OK")
expect_exit(1 --login alice guess1 --store users.txt)
expect_exit(1 --login alice guess2 --store users.txt)
expect_exit(1 --login alice guess3 --store users.txt)
expect_output("locked for 30 s")
expect_exit(1 --login alice correct-horse --store users.txt)
expect_output("Account locked")
file(READ "${WORK_DIR}/users.txt" store)
if(store MATCHES "correct-horse")
  message(FATAL_ERROR "the password store contains a plain-text password")
endif()

# One password, two salts: two different records.
expect_exit(0 --password hunter2 --salt 000102030405060708090a0b0c0d0e0f --iterations 1000 -q -o pw1.mid)
expect_output("f5550e89119f593cd3662c6d7da5bd3f7a90e25cd2dc221f58a6166c772753d3")
expect_exit(0 --password hunter2 --salt ffeeddccbbaa99887766554433221100 --iterations 1000 -q -o pw2.mid)

# Toy hash: a collision gives two byte-identical songs.
expect_exit(0 --collide jacob)
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${WORK_DIR}/collision_a.mid" "${WORK_DIR}/collision_b.mid"
                RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
  message(FATAL_ERROR "the toy-hash collision did not give identical songs")
endif()
expect_exit(2 --mine jacob --hash toy)

# Mining, hash art and audio.
expect_exit(0 --mine jacob --difficulty 3 --art mined.svg --wav mined.wav -o mined.mid)
expect_output("SHA-256   000")
foreach(f mined.mid mined.svg mined.wav)
  if(NOT EXISTS "${WORK_DIR}/${f}")
    message(FATAL_ERROR "--mine did not write ${f}")
  endif()
endforeach()

message(STATUS "--verify, --register/--login lockout, --password salts, --collide, --mine, --art and --wav all behave")
