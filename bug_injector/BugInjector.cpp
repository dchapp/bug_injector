// Resources Used: 
// 1. https://www.cs.cornell.edu/~asampson/blog/llvm.html
// 2. https://sites.google.com/site/arnamoyswebsite/Welcome/updates-news/llvmpasstoinsertexternalfunctioncalltothebitcode

// Standard C headers
#include <stdlib.h> // For RNG
#include <inttypes.h> 

// Standard headers
#include <regex> 
#include <unordered_map>
#include <fstream>

// Non-standard headers 
//#include <toml.h> // Sucks b/c TOML is uncommon?
//#include <boost/property_tree/ptree.hpp>  // Can't use b/c -fno_rtti 
//#include <boost/property_tree/json_parser.hpp> 
#include <nlohmann/json.hpp> 
using json = nlohmann::json; 

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

using namespace llvm;

static int n_bugs = 3;
static int n_injected = 0;
static double injection_probability = 0.01; 
static double roll;

#define DEBUG
//#define VDEBUG

typedef struct rng_info {
  bool is_seed_fixed;
  uint64_t seed;
} rng_info_t;

typedef struct bug_info {
  std::string type;
  uint64_t num;
  uint64_t max_per_function;
  uint64_t max_per_basic_block;
  Constant* bug_function;
  std::vector<uint64_t> bug_function_args; 
} bug_info_t;

typedef struct config {
  rng_info_t rng;
  std::unordered_map< std::string, bug_info_t > bugs;
} config_t; 

const config_t parse_config(std::string config_path)
{
  // Parse the configuration file 
  std::ifstream i(config_path);
  json config_json;
  i >> config_json;
  // Configuration to populate
  config_t config;
  // Extract RNG information
  config.rng.is_seed_fixed = (bool) config_json["rng"]["fixed"];
  config.rng.seed = (uint64_t) config_json["rng"]["seed"];
  // Extract bug information
  uint64_t n_bug_types = config_json["bugs"].size();
  for (int i = 0; i < n_bug_types; i++) 
  {
    // Extract constraints for where bugs can be placed 
    std::string bug_type(config_json["bugs"][i]["type"]);
    bug_info_t bug_info;
    bug_info.num = (uint64_t) config_json["bugs"][i]["num"];
    bug_info.max_per_function = (uint64_t) config_json["bugs"][i]["max_per_function"];
    bug_info.max_per_basic_block = (uint64_t) config_json["bugs"][i]["max_per_basic_block"];
    // If this bug function takes arguments, unpack them here 
    uint64_t n_args = config_json["bugs"][i]["bug_function_args"].size();
    for (int j = 0; j < n_args; j++)
    {
      bug_info.bug_function_args.push_back( (uint64_t)config_json["bugs"][i]["bug_function_args"][j] );
    }
    // Insert this bug's information into the map from bug names to bug info
    config.bugs.insert( {bug_type, bug_info} ); 
  }
  return (const config_t) config; 
} 

void print_config(const config_t config) 
{
  errs() << "\nBug-Injector Pass Configuration:\n";
  errs() << "================================\n";
  errs() << "RNG Configuration:\n";
  errs() << "================================\n";
  errs() << "\t- Using fixed seed?: " << config.rng.is_seed_fixed << "\n";
  errs() << "\t- Seed: " << config.rng.seed << "\n";
  errs() << "================================\n";
  errs() << "Bug Configurations:\n";
  errs() << "================================\n";
  for ( auto bug : config.bugs )
  {
    std::string bug_name = bug.first;
    bug_info bug_info = bug.second;
    errs() << "\t- Bug type: " << bug_name << "\n";
    errs() << "\t\t- Number of bugs: " << bug_info.num << "\n";
    errs() << "\t\t- Max bugs per function: " << bug_info.max_per_function << "\n";
    errs() << "\t\t- Max bugs per basic block: " << bug_info.max_per_basic_block << "\n";
    errs() << "\t\t- Bug function arguments:\n";
    for ( auto arg : bug_info.bug_function_args )
    {
      errs() << "\t\t\t- " << arg << "\n";
    }
    errs() << "\n"; 
  }
  errs() << "================================\n\n";
}

namespace {

  /* Module Pass 
   */
  struct BugInjectorPass : public ModulePass {
    static char ID; 
    config_t config;
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t> >func_to_bugcounts;
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t> >bb_to_bugcounts;
    std::unordered_map<std::string, Constant*> bugs; 

    BugInjectorPass() : ModulePass(ID)
    {
      // Get configuration details for this pass
      std::string config_path = getConfPath();
      // Parse and validate configuration
      config = parse_config(config_path);
      // Set up RNG, look up bug functions, etc...
      init();
#ifdef DEBUG
      print_config(config); 
#endif
    }

    virtual bool runOnModule(Module &M) override; 
    void init(); 
    std::string getConfPath(); 
    bool runOnFunctionFirst(Function &F);
    bool runOnFunction(Function &F);
    bool legalToInject(Function &F, BasicBlock &B, const std::string& bug_type);
    Constant* lookupHangMSFunc(Function &F);
    Constant* lookupHangFunc(Function &F);
    Constant* lookupFPEFunc(Function &F);
    //void lookupBugFunctions(Module &M);
    void lookupBugFunctions(Function &F);
  };

  std::string BugInjectorPass::getConfPath()
  {
    std::string default_config_path = "./config/default.json";
    std::string config_path;
    char* env_var;
    env_var = getenv("BUG_INJECTOR_CONFIG");
    if (env_var == NULL) {
      config_path = default_config_path;
#ifdef DEBUG
      errs() << "No configuration file specified. Using default configuration located at: "
             << default_config_path << "\n";
#endif
    } else {
      config_path = env_var; 
#ifdef DEBUG
      errs() << "Using provided configuration file: " << config_path << "\n"; 
#endif
    }
    return config_path; 
  }

  void BugInjectorPass::lookupBugFunctions(Function &F)
  {
    //for (int i = 0; i < config.bugs.size(); i++) 
    //{
    //  // Get bug function name
    //  std::string bug_type = config.bugs[i].type; 
    //  // Get a context... still not totally sure what this does
    //  LLVMContext &context = F.getContext();
    //  // Get types of its arguments, if any
    //  std::vector<Type*> paramTypes;
    //  for (int j = 0; j < config.bugs[i].bug_function_args.size(); j++) 
    //  {
    //    paramTypes.push_back( Type::getInt32Ty(context) );
    //  }
    //  // Get return type. For our bug functions, this should always be void
    //  Type *retType = Type::getVoidTy(context);
    //  // Construct bug function type and bug function
    //  FunctionType *bugFunctionType = FunctionType::get(retType, paramTypes, false);
    //  //Constant *bugFunction = M.getOrInsertFunction(bug_type, bugFunctionType);
    //  Constant *bugFunction = F.getParent()->getOrInsertFunction(bug_type, bugFunctionType);
    //  // Insert into bug function map 
    //  bugs.insert( {bug_type, bugFunction} );
    //}
  }

  void BugInjectorPass::init()
  {
    // Seed RNG so that we can reproduce randomly injecting bug instructions
    if (config.rng.is_seed_fixed) {
      srand(config.rng.seed);
    } else {
      srand(time(NULL));
    }
  }

  //Constant* BugInjectorPass::lookupHangMSFunc(Function &F) 
  //{
  //  LLVMContext &context = F.getContext();
  //  std::vector<Type*> paramTypes = { Type::getInt32Ty(context) };
  //  Type *retType = Type::getVoidTy(context);
  //  FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
  //  Constant *bugFunc = F.getParent()->getOrInsertFunction("hang_ms", bugFuncType);
  //  return bugFunc; 
  //}
  //
  //Constant* BugInjectorPass::lookupHangFunc(Function &F) 
  //{
  //  LLVMContext &context = F.getContext();
  //  std::vector<Type*> paramTypes = { };
  //  Type *retType = Type::getVoidTy(context);
  //  FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
  //  //Constant *bugFunc = F.getParent()->getOrInsertFunction(conf.bug_type, bugFuncType);
  //  Constant *bugFunc = F.getParent()->getOrInsertFunction("hang", bugFuncType);
  //  return bugFunc; 
  //}
  //
  //Constant* BugInjectorPass::lookupFPEFunc(Function &F) 
  //{
  //  LLVMContext &context = F.getContext();
  //  std::vector<Type*> paramTypes = { };
  //  Type *retType = Type::getVoidTy(context);
  //  FunctionType *bugFuncType = FunctionType::get(retType, paramTypes, false);
  //  Constant *bugFunc = F.getParent()->getOrInsertFunction("fpe", bugFuncType);
  //  return bugFunc; 
  //}

  bool BugInjectorPass::legalToInject(Function &F, BasicBlock &B, const std::string& bug_type) 
  {
    //// Don't inject if this is a function call added by OpenMP
    //std::regex ompFuncPattern("\\.omp_[a-z_0-9\\.]+");
    //if ( std::regex_match(F.getName().str(), ompFuncPattern) ) {
    //  return false;
    //} 
    //// Don't inject if this basic block or its enclosing function are already
    //// at their maximum bug count
    //std::string funcName = F.getName().str();
    //std::string bbName = B.getName().str();
    //if (func_to_bugcounts.at(funcName).at(bug_type) >= config.max_bugs_per_function || 
    //    bb_to_bugcount.at(bbName) >= conf.max_bugs_per_basic_block ) {
    //  return false;
    //}
    //return true;
  }

  bool BugInjectorPass::runOnModule(Module &M) 
  {
    errs() << "In Module: " << M.getName() << "\n";
    errs() << "Looking up bug functions...\n";
    bool out; 

    //lookupBugFunctions(M); 
    errs() << "Done looking up bug functions\n";
    //for (auto kvp : bugs) 
    //{
    //  errs() << "Bug function: " << kvp.first << "\n"; 
    //}
   
    // One loop over functions 
    for (auto &F : M) 
    { 
      //out = BugInjectorPass::runOnFunctionFirst(F); 
    }
    
    for (auto &F : M) 
    { 
      //lookupBugFunctions(F); 
      //out = BugInjectorPass::runOnFunction(F); 
    }
    
    return false; 
  }
  
  bool BugInjectorPass::runOnFunctionFirst(Function &F) 
  {
    int bb_idx = 0;
    errs() << "Running first on function: " << F.getName() << "\n";
    for (auto &B : F) 
    {
      errs() << "Running first on basic block: " << bb_idx << "\n";
      int instruction_idx = 0;
      for (auto &I : B) 
      {
        //errs() << "Running first on instruction: " << instruction_idx << "\n";
        instruction_idx++; 
      }
    }
    return false; 
  }

  bool BugInjectorPass::runOnFunction(Function &F) 
  {
    // Set initial bug count for this function 
    //func_to_bugcount[F.getName().str()] = 0;
    //// Get the the bug functions we will inject
    //Constant *hangFunc = lookupHangFunc(F); 
    //Constant *hangmsFunc = lookupHangMSFunc(F);
    //Constant *fpeFunc = lookupFPEFunc(F); 
    // Loop over basic blocks
    int bb_idx = 0; 
    int in_idx = 0;
    double injection_probability = 0.1; 
    for (auto &B : F) 
    {
      // Set initial bug count for this basic block
      //bb_to_bugcount[B.getName().str()] = 0;
      // Check whether injection is allowed
      if (legalToInject(F, B, F.getName().str())) {
        // If it is, loop over instructions
        for (auto &I : B) {
          // And check to see if we actually inject here or not
          roll = static_cast<double> (rand()) / static_cast<double> (RAND_MAX);
          if (roll < injection_probability) {
#ifdef DEBUG
            errs() << "Error of type: hang_ms"  
                   << ", injected at function: " << F.getName() 
                   << ", basic block: " << bb_idx 
                   << ", instruction: " << in_idx << "\n"; 
#endif
            
            // Set injector and make args for bug functions
            IRBuilder<> builder(&I);
            ConstantInt *hang_time = builder.getInt32(17);
            
            Constant* hangmsFunc = bugs.find("hang_ms")->second; 

            // Inject bugs
            builder.CreateCall(hangmsFunc, hang_time);
            //builder.CreateCall(hangFunc, hang_time);
            //builder.CreateCall(fpeFunc, hang_time);
            
            // Update bug counts
            //func_to_bugcount[F.getName().str()]++; 
            //bb_to_bugcount[B.getName().str()]++;
          }
          in_idx++;
        }
        bb_idx++;
      }
    }
    return false;
  }
}

char BugInjectorPass::ID = 0;
//char BugInjectorPass::ID = 0;

// Automatically enable the pass.
// http://adriansampson.net/blog/clangpass.html
static void 
registerBugInjectorPass(const PassManagerBuilder &, legacy::PassManagerBase &PM) 
{
  PM.add(new BugInjectorPass());
}

/* The below works for registering a FunctionPass, but not a ModulePass */
//static RegisterStandardPasses 
//RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible, registerBugInjectorPass);


/* These work for registering a ModulePass
 * Specifically, "extension point" "EP_EarlyAsPossible" does not work here 
 * (segfaults) but extension point "EP_ModuleOptimizerEarly" works. 
 * See discussion at: https://github.com/sampsyo/llvm-pass-skeleton/issues/7
 * for more details. 
 */
static RegisterStandardPasses 
RegisterMyPass(PassManagerBuilder::EP_ModuleOptimizerEarly, registerBugInjectorPass);

static RegisterStandardPasses
RegisterMyPass0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerBugInjectorPass);
