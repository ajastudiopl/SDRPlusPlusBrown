file(REMOVE_RECURSE
  "lib/libcorrect.dylib"
  "lib/libcorrect.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/correct.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
