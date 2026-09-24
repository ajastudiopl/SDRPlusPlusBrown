file(REMOVE_RECURSE
  "lib/libcorrect_static.a"
  "lib/libcorrect_static.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/correct_static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
