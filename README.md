## Bug Injector
This LLVM pass is meant to be used to create test cases for an OMPT-based 
bug-localization tool for OpenMP tasking applications.

The bug-localization tool can be found at: https://github.com/TauferLab/OMPT_dependency_tracker

### Use
This LLVM pass reads in a TOML configuration file that describes what kind of 
bugs to inject, how many to inject, and any rules for injection patterns. The 
bug code itself is contained in `error_lib`. Currently the only available bug
is a hang--either for a given number of milliseconds, or until the program is
terminated. 

