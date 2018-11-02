// Resources Used: 
// 1. https://www.cs.cornell.edu/~asampson/blog/llvm.html
// 2. https://sites.google.com/site/arnamoyswebsite/Welcome/updates-news/llvmpasstoinsertexternalfunctioncalltothebitcode

#include <stdlib.h> 
#include <regex> 

#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h" 
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Support/RandomNumberGenerator.h"


using namespace llvm;

static int n_bugs = 3;
static int n_injected = 0;
static double injection_probability = 0.01; 
static double roll;

#define FIXED_SEED
#define SEED 17 
#define DEBUG
//#define VDEBUG

namespace {
  struct BugInjectorPass : public FunctionPass {
    static char ID;
    BugInjectorPass() : FunctionPass(ID) 
    {
#ifdef FIXED_SEED
      srand(SEED); 
#else 
      srand(time(NULL));
#endif
    }
    
    virtual bool runOnFunction(Function &F) {
#ifdef VDEBUG
      errs() << "Handling Function: " << F.getName() << "\n";
#endif
      // Exit early if we're in an OpenMP function
      std::regex ompFuncPattern("\\.omp_[a-z_0-9\\.]+");
      if ( std::regex_match(F.getName().str(), ompFuncPattern) ) {
#ifdef VDEBUG
        errs() << "Found OMP function. Skipping.\n"; 
#endif
        return false;
      }

      // Get the "hang" bug function from library
      LLVMContext &context = F.getContext();
      std::vector<Type*> paramTypes = { Type::getInt32Ty(context) };
      Type *retType = Type::getVoidTy(context);
      FunctionType *hangFuncType = FunctionType::get(retType, paramTypes, false);
      Constant *hangFunc = F.getParent()->getOrInsertFunction("hang", hangFuncType);

      // Loop over basic blocks
      int bb_idx = 0; 
      int in_idx = 0;
      for (auto &B : F) 
      {
        bool bb_is_tainted = false;
        // Loop over instructions 
        for (auto &I : B)
        {
          // Decide whether to do error injection right here
          roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
          if (roll < injection_probability && !bb_is_tainted) 
          {
#ifdef DEBUG
            errs() << "Error injected at function: " << F.getName() 
                   << ", basic block: " << bb_idx 
                   << " instruction: " << in_idx << "\n"; 
#endif
            IRBuilder<> builder(&I);
            ConstantInt *hang_time = builder.getInt32(17);
            builder.CreateCall(hangFunc, hang_time);
            bb_is_tainted = true;
          }
          in_idx++;
        }
        bb_idx++;
      }
    }
  };
  
}

char BugInjectorPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void 
registerBugInjectorPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) 
{
  PM.add(new BugInjectorPass());
}
static RegisterStandardPasses 
RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible, registerBugInjectorPass);
