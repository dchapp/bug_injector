// Resources Used: 
// 1. https://www.cs.cornell.edu/~asampson/blog/llvm.html
// 2. https://sites.google.com/site/arnamoyswebsite/Welcome/updates-news/llvmpasstoinsertexternalfunctioncalltothebitcode

// Standard headers
#include <stdlib.h> // For RNG
#include <regex> 

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
  // Seed RNG
  if (conf.fixed_seed) {
    srand(conf.rng_seed);
  } else {
    srand(time(NULL));
  }
}

namespace {
  struct BugInjectorPass : public FunctionPass {
    static char ID;
    config_t conf;
    int n_bugs_injected = 0;

    BugInjectorPass() : FunctionPass(ID) 
    {
      // Parse and validate configuration
      std::string conf_path = "./config/config.toml";
      conf = parse_config(conf_path);
      print_config(conf); 
      
      // Set up for bug injection
      init(conf);
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
      Constant *hangFunc = F.getParent()->getOrInsertFunction(conf.bug_type, hangFuncType);

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
          if (roll < conf.injection_probability && !bb_is_tainted) 
          {
#ifdef DEBUG
            errs() << "Error of type: " << conf.bug_type 
                   << ", injected at function: " << F.getName() 
                   << ", basic block: " << bb_idx 
                   << ", instruction: " << in_idx << "\n"; 
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
