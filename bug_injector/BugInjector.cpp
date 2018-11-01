#include <stdlib.h> 

// Includes from example pass at: https://www.cs.cornell.edu/~asampson/blog/llvm.html
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

// Added to make module pass code compile
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
//#include "llvm/IR/Type.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IRBuilder.h" 
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/Support/RandomNumberGenerator.h"

// Includes from example pass at: https://sites.google.com/site/arnamoyswebsite/Welcome/updates-news/llvmpasstoinsertexternalfunctioncalltothebitcode
//#include "llvm/Pass.h"
//#include "llvm/Module.h"
//#include "llvm/Function.h"
//#include "llvm/Support/raw_ostream.h"
//#include "llvm/Type.h"
//#include "llvm/Instructions.h"
//#include "llvm/Instruction.h"
//#include "llvm/IRBuilder.h"

using namespace llvm;

static int n_bugs = 3;
static int n_injected = 0;
static double injection_probability = 0.1; 
static double roll;

namespace {
  //struct BugInjectorPass : public FunctionPass {
  //  static char ID;
  //  BugInjectorPass() : FunctionPass(ID) {}
  //  virtual bool runOnFunction(Function &F) {
  //    srand(static_cast<unsigned>(time(0)));
  //    errs() << "Wreaking havoc in function: " << F.getName() << "!\n";
  //    for (auto &B : F) {
  //      int bb_idx = 0;
  //      for (auto &I : B) {
  //        errs() << "Considering mischief for basic block: " << bb_idx << "!\n";
  //        roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
  //        if (roll < injection_probability) {
  //          errs() << "Injecting error!!!\n"; 
  //        }
  //        bb_idx++;
  //      }
  //    }
  //  }
  //};
  
  struct BugInjectorPass : public FunctionPass {
    static char ID;
    BugInjectorPass() : FunctionPass(ID) 
    {
      srand(17); 
    }
    
    virtual bool runOnFunction(Function &F) {
      //errs() << "Wreaking havoc in function: " << F.getName() << "!\n";
     
      // RNG stuff
      auto rng = F.getParent()->createRNG(this);
      //errs() << "Function: " << F.getName() << ", RNG: " << (*rng)() << "\n";

      // Get the "hang" bug function from library
      LLVMContext &context = F.getContext();
      std::vector<Type*> paramTypes = { Type::getInt32Ty(context) };
      Type *retType = Type::getVoidTy(context);
      FunctionType *hangFuncType = FunctionType::get(retType, paramTypes, false);
      Constant *hangFunc = F.getParent()->getOrInsertFunction("hang", hangFuncType);

      // Do bug injection
      int bb_idx = 0; 
      int in_idx = 0;
      for (auto &B : F) 
      {
        bool bb_is_tainted = false;
        for (auto &I : B)
        {
          // Decide whether to do error injection right here
          //errs() << "Considering wrecking your day in basic block: " 
          //       << bb_idx << ", instruction: " << in_idx <<  "\n";
          
          roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
          if (roll < injection_probability && !bb_is_tainted) 
          {
            errs() << "Error injected at function: " << F.getName() << ", basic block: " << bb_idx << " instruction: " << in_idx << "\n"; 

            IRBuilder<> builder(&I);
            ConstantInt *hang_time = builder.getInt32(17);
            //ConstantInt *hang_time = builder.getInt32(0); // Infinite hang
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
