// Resources Used: 
// 1. https://www.cs.cornell.edu/~asampson/blog/llvm.html
// 2. https://sites.google.com/site/arnamoyswebsite/Welcome/updates-news/llvmpasstoinsertexternalfunctioncalltothebitcode

// Standard headers
#include <stdlib.h> // For RNG
#include <regex> 
#include <unordered_map>

// Non-standard headers 
#include <toml.h>

// LLVM specific headers
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

//#include "llvm/Support/RandomNumberGenerator.h"


using namespace llvm;

static int n_bugs = 3;
static int n_injected = 0;
static double injection_probability = 0.01; 
static double roll;

#define DEBUG
//#define VDEBUG

typedef struct config {
  bool fixed_seed;
  int rng_seed;
  std::string bug_type;
  int n_bugs;
  double injection_probability;
  int max_bugs_per_function;
  int max_bugs_per_basic_block;
} config_t; 

const config_t parse_config(std::string conf_path)
{
  // Parse and validate
  std::ifstream ifs(conf_path);
  toml::ParseResult pr = toml::parse(ifs);
  if (!pr.valid()) {
    errs() << pr.errorReason << "\n";
    exit(1); 
  }
  const toml::Value& v = pr.value;

  // Populate config
  config_t conf;
  conf.fixed_seed = v.find("rng.fixed_seed")->as<bool>();
  conf.rng_seed = v.find("rng.rng_seed")->as<int>();
  conf.bug_type = v.find("bug.bug_type")->as<std::string>();
  conf.n_bugs = v.find("bug.n_bugs")->as<int>();
  conf.injection_probability = v.find("bug.injection_probability")->as<double>();
  conf.max_bugs_per_function = v.find("bug.max_bugs_per_function")->as<int>();
  conf.max_bugs_per_basic_block = v.find("bug.max_bugs_per_basic_block")->as<int>();

  return (const config_t) conf; 
} 

void print_config(const config_t conf) 
{
  errs() << "\nBug-Injector Pass Configuration:\n";
  errs() << "================================\n";
  errs() << "Fixed seed: " << conf.fixed_seed << "\n";
  errs() << "Seed: " << conf.rng_seed << "\n";
  errs() << "Bug type: " << conf.bug_type << "\n";
  errs() << "Number of bugs: " << conf.n_bugs << "\n";
  errs() << "Injection probability: " << conf.injection_probability << "\n";
  errs() << "Max bugs per function: " << conf.max_bugs_per_function << "\n";
  errs() << "Max bugs per basic block: " << conf.max_bugs_per_basic_block<< "\n";
  errs() << "================================\n\n";
}

void init(const config_t conf) 
{
  // Seed RNG so that we can reproduce randomly injecting bug instructions
  if (conf.fixed_seed) {
    srand(conf.rng_seed);
  } else {
    srand(time(NULL));
  }
}

namespace {

//  struct BugInjectorPass : public FunctionPass {
//    static char ID;
//    config_t conf;
//    int n_bugs_injected = 0;
//    std::unordered_map<std::string, int> func_to_bugcount;
//    std::unordered_map<std::string, int> bb_to_bugcount;
//
//    BugInjectorPass() : FunctionPass(ID) 
//    {
//      // Parse and validate configuration
//      std::string conf_path = "./config/config.toml";
//      conf = parse_config(conf_path);
//      print_config(conf); 
//      
//      // Set up for bug injection
//      init(conf);
//    }
//
//    Constant* lookupHangMSFunc(Function &F) 
//    {
//      LLVMContext &context = F.getContext();
//      std::vector<Type*> paramTypes = { Type::getInt32Ty(context) };
//      Type *retType = Type::getVoidTy(context);
//      FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
//      Constant *bugFunc = F.getParent()->getOrInsertFunction("hang_ms", bugFuncType);
//      return bugFunc; 
//    }
//    
//    Constant* lookupHangFunc(Function &F) 
//    {
//      LLVMContext &context = F.getContext();
//      std::vector<Type*> paramTypes = { };
//      Type *retType = Type::getVoidTy(context);
//      FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
//      //Constant *bugFunc = F.getParent()->getOrInsertFunction(conf.bug_type, bugFuncType);
//      Constant *bugFunc = F.getParent()->getOrInsertFunction("hang", bugFuncType);
//      return bugFunc; 
//    }
//    
//    Constant* lookupFPEFunc(Function &F) 
//    {
//      LLVMContext &context = F.getContext();
//      std::vector<Type*> paramTypes = { };
//      Type *retType = Type::getVoidTy(context);
//      FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
//      Constant *bugFunc = F.getParent()->getOrInsertFunction("fpe", bugFuncType);
//      return bugFunc; 
//    }
//
//    // Decide whether to inject a bug in a basic block, and if so, at which
//    // instruction.
//    bool legalToInject(Function &F, BasicBlock &B) 
//    {
//      // Don't inject if this is a function call added by OpenMP
//      std::regex ompFuncPattern("\\.omp_[a-z_0-9\\.]+");
//      if ( std::regex_match(F.getName().str(), ompFuncPattern) ) {
//        return false;
//      } 
//      
//      // Don't inject if this basic block or its enclosing function are already
//      // at their maximum bug count
//      std::string funcName = F.getName().str();
//      std::string bbName = B.getName().str();
//      if (func_to_bugcount.at(funcName) >= conf.max_bugs_per_function || 
//          bb_to_bugcount.at(bbName) >= conf.max_bugs_per_basic_block ) {
//        return false;
//      }
//
//      return true;
//    }
//
//    virtual bool runOnFunction(Function &F) {
//      // Set initial bug count for this function 
//      func_to_bugcount[F.getName().str()] = 0;
//      
//      // Get the the bug functions we will inject
//      Constant *hangFunc = lookupHangFunc(F); 
//      Constant *hangmsFunc = lookupHangMSFunc(F);
//      Constant *fpeFunc = lookupFPEFunc(F); 
//
//      // Loop over basic blocks
//      int bb_idx = 0; 
//      int in_idx = 0;
//      for (auto &B : F) 
//      {
//        // Set initial bug count for this basic block
//        bb_to_bugcount[B.getName().str()] = 0;
//        // Check whether injection is allowed
//        if (legalToInject(F, B)) {
//          // If it is, loop over instructions
//          for (auto &I : B) {
//            // And check to see if we actually inject here or not
//            roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
//            if (roll < conf.injection_probability) {
//#ifdef DEBUG
//              errs() << "Error of type: " << conf.bug_type 
//                     << ", injected at function: " << F.getName() 
//                     << ", basic block: " << bb_idx 
//                     << ", instruction: " << in_idx << "\n"; 
//#endif
//              // Set injector and make args for bug functions
//              IRBuilder<> builder(&I);
//              ConstantInt *hang_time = builder.getInt32(17);
//             
//              // Inject bugs
//              //builder.CreateCall(hangFunc);
//              builder.CreateCall(hangmsFunc, hang_time);
//              //builder.CreateCall(fpeFunc);
//
//              // Update bug counts
//              func_to_bugcount[F.getName().str()]++; 
//              bb_to_bugcount[B.getName().str()]++;
//            }
//            in_idx++;
//          }
//          bb_idx++;
//        }
//      }
//    }
//  };
  
  /* Module Pass 
   */
  struct BugInjectorModulePass : public ModulePass {
    static char ID; 
    config_t conf;
    int n_bugs_injected = 0;
    std::unordered_map<std::string, int> func_to_bugcount;
    std::unordered_map<std::string, int> bb_to_bugcount;

    BugInjectorModulePass() : ModulePass(ID)
    {
      // Parse and validate configuration
      std::string conf_path = "./config/config.toml";
      conf = parse_config(conf_path);
      print_config(conf); 
      // Set up for bug injection
      init(conf);
    }
    virtual bool runOnModule(Module &M) override; 
    bool runOnFunction(Function &F);
    bool legalToInject(Function &F, BasicBlock &B);
    Constant* lookupHangMSFunc(Function &F);
    Constant* lookupHangFunc(Function &F);
    Constant* lookupFPEFunc(Function &F);
  };
  
  //BugInjectorModulePass::BugInjectorModulePass() 
  //{
  //  // Parse and validate configuration
  //  std::string conf_path = "./config/config.toml";
  //  conf = parse_config(conf_path);
  //  print_config(conf); 
  //  // Set up for bug injection
  //  init(conf);
  //}

  Constant* BugInjectorModulePass::lookupHangMSFunc(Function &F) 
  {
    LLVMContext &context = F.getContext();
    std::vector<Type*> paramTypes = { Type::getInt32Ty(context) };
    Type *retType = Type::getVoidTy(context);
    FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
    Constant *bugFunc = F.getParent()->getOrInsertFunction("hang_ms", bugFuncType);
    return bugFunc; 
  }
  
  Constant* BugInjectorModulePass::lookupHangFunc(Function &F) 
  {
    LLVMContext &context = F.getContext();
    std::vector<Type*> paramTypes = { };
    Type *retType = Type::getVoidTy(context);
    FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
    //Constant *bugFunc = F.getParent()->getOrInsertFunction(conf.bug_type, bugFuncType);
    Constant *bugFunc = F.getParent()->getOrInsertFunction("hang", bugFuncType);
    return bugFunc; 
  }
  
  Constant* BugInjectorModulePass::lookupFPEFunc(Function &F) 
  {
    LLVMContext &context = F.getContext();
    std::vector<Type*> paramTypes = { };
    Type *retType = Type::getVoidTy(context);
    FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
    Constant *bugFunc = F.getParent()->getOrInsertFunction("fpe", bugFuncType);
    return bugFunc; 
  }

  bool BugInjectorModulePass::legalToInject(Function &F, BasicBlock &B) 
  {
    // Don't inject if this is a function call added by OpenMP
    std::regex ompFuncPattern("\\.omp_[a-z_0-9\\.]+");
    if ( std::regex_match(F.getName().str(), ompFuncPattern) ) {
      return false;
    } 
    // Don't inject if this basic block or its enclosing function are already
    // at their maximum bug count
    std::string funcName = F.getName().str();
    std::string bbName = B.getName().str();
    if (func_to_bugcount.at(funcName) >= conf.max_bugs_per_function || 
        bb_to_bugcount.at(bbName) >= conf.max_bugs_per_basic_block ) {
      return false;
    }
    return true;
  }

  bool BugInjectorModulePass::runOnModule(Module &M) 
  {
    errs() << "In Module: " << M.getName() << "\n";
    bool out; 
    for (auto &F : M) 
    { 
      out = BugInjectorModulePass::runOnFunction(F); 
    }
    return false; 
  }

  bool BugInjectorModulePass::runOnFunction(Function &F) 
  {
    // Set initial bug count for this function 
    func_to_bugcount[F.getName().str()] = 0;
    // Get the the bug functions we will inject
    Constant *hangFunc = lookupHangFunc(F); 
    Constant *hangmsFunc = lookupHangMSFunc(F);
    Constant *fpeFunc = lookupFPEFunc(F); 
    // Loop over basic blocks
    int bb_idx = 0; 
    int in_idx = 0;
    for (auto &B : F) 
    {
      // Set initial bug count for this basic block
      bb_to_bugcount[B.getName().str()] = 0;
      // Check whether injection is allowed
      if (legalToInject(F, B)) {
        // If it is, loop over instructions
        for (auto &I : B) {
          // And check to see if we actually inject here or not
          roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
          if (roll < conf.injection_probability) {
#ifdef DEBUG
            errs() << "Error of type: " << conf.bug_type 
                   << ", injected at function: " << F.getName() 
                   << ", basic block: " << bb_idx 
                   << ", instruction: " << in_idx << "\n"; 
#endif
            // Set injector and make args for bug functions
            IRBuilder<> builder(&I);
            ConstantInt *hang_time = builder.getInt32(17);
            // Inject bugs
            builder.CreateCall(hangmsFunc, hang_time);
            // Update bug counts
            func_to_bugcount[F.getName().str()]++; 
            bb_to_bugcount[B.getName().str()]++;
          }
          in_idx++;
        }
        bb_idx++;
      }
    }
  }
}

char BugInjectorModulePass::ID = 0;
//char BugInjectorPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void 
registerBugInjectorPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) 
{
  //PM.add(new BugInjectorPass());
  PM.add(new BugInjectorModulePass());
}

/* These work for registering a FunctionPass
 */
//static RegisterStandardPasses 
//RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible, registerBugInjectorPass);


/* These work for registering a ModulePass
 * Specifically, entry point "EP_EarlyAsPossible" does not work here
 */
static RegisterStandardPasses 
RegisterMyPass(PassManagerBuilder::EP_ModuleOptimizerEarly, registerBugInjectorPass);

static RegisterStandardPasses
RegisterMyPass0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerBugInjectorPass);
