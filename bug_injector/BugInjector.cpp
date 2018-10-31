#include <stdlib.h> 

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
using namespace llvm;

static int n_bugs = 3;
static int n_injected = 0;
static double injection_probability = 0.1; 
static double roll;

namespace {
  struct BugInjectorPass : public FunctionPass {
    static char ID;
    BugInjectorPass() : FunctionPass(ID) {}

    virtual bool runOnFunction(Function &F) {
      //errs() << "Seeding RNG...\n";
      srand(static_cast<unsigned>(time(0)));

      errs() << "Wreaking havoc in function: " << F.getName() << "!\n";
      // Loop over the basic blocks of function F?
      for (auto &B : F) {
        int bb_idx = 0;
        // Loop over the instructions of basic block B? 
        for (auto &I : B) {
          errs() << "Considering mischief for basic block: " << bb_idx << "!\n";
          roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
          if (roll < injection_probability) {
            errs() << "Injecting error!!!\n"; 
          }
          bb_idx++;
        }
      }
      //return false;
    }


  };
}

char BugInjectorPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void registerBugInjectorPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new BugInjectorPass());
}
static RegisterStandardPasses
  RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                 registerBugInjectorPass);
