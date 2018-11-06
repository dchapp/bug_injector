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
    std::unordered_map<uint64_t, std::unordered_map<std::string, uint64_t> >bb_to_bugcounts;

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
    bool legalToInject(Function &F, uint64_t bb_idx, const std::string& bug_type);
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
    for ( auto bug : config.bugs ) 
    {
      // Unpack
      std::string bug_name = bug.first;
      bug_info_t bug_info = bug.second;
      // Get context. Needed for using LLVM in threaded setting
      LLVMContext &context = F.getContext();
      // Get types of bug function arguments, if any
      std::vector<Type*> paramTypes;
      for ( auto arg : bug_info.bug_function_args )
      {
        paramTypes.push_back( Type::getInt32Ty(context) );
      }
      // Get return type. For bug functions, this is always void. 
      Type *retType = Type::getVoidTy(context);
      // Construct type for bug function 
      FunctionType *bugFunctionType = FunctionType::get(retType, paramTypes, false);
      // Actually look up the function 
      Constant *bugFunction = F.getParent()->getOrInsertFunction(bug_name, bugFunctionType);
      // Update bug info
      bug_info.bug_function = bugFunction; 
      // Update the map
      config.bugs[bug_name] = bug_info; 
    }
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

  bool BugInjectorPass::legalToInject(Function &F, uint64_t bb_idx, const std::string& bug_type) 
  {
    // Don't inject if this is a function call added by OpenMP
    std::regex ompFuncPattern("\\.omp_[a-z_0-9\\.]+");
    if ( std::regex_match(F.getName().str(), ompFuncPattern) ) {
      return false;
    } 
    // Don't inject if this basic block or its enclosing function are already
    // at their maximum bug count
    std::string funcName = F.getName().str();

    errs() << "Number of bugs of type: " << bug_type
           << " allowed in function: " << funcName
           << " , basic block: " << bb_idx
           << " = " << config.bugs.at(bug_type).max_per_basic_block << "\n";

    errs() << "Number of bugs of type: " << bug_type
           << " currently injected in function: " << funcName
           << " , basic block: " << bb_idx
           << " = " << bb_to_bugcounts.at(bb_idx).at(bug_type) << "\n";

    if (func_to_bugcounts.at(funcName).at(bug_type) >= config.bugs.at(bug_type).max_per_function || 
        bb_to_bugcounts.at(bb_idx).at(bug_type) >= config.bugs.at(bug_type).max_per_basic_block ) {
      return false;
    }
    return true;
  }

  bool BugInjectorPass::runOnModule(Module &M) 
  {
    errs() << "In Module: " << M.getName() << "\n";
    bool out; 

    // One loop over functions 
    for (auto &F : M) 
    { 
      //out = BugInjectorPass::runOnFunctionFirst(F); 
    }
    
    for (auto &F : M) 
    { 
      lookupBugFunctions(F); 
      out = BugInjectorPass::runOnFunction(F); 
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
        instruction_idx++; 
      }
    }
    return false; 
  }

  bool BugInjectorPass::runOnFunction(Function &F) 
  {

    // Set initial bug counts for this function 
    for ( auto bug_type : config.bugs )
    {
      func_to_bugcounts[F.getName().str()][bug_type.first] = 0;
    }

    // Loop over basic blocks
    int bb_idx = 0; 
    int in_idx = 0;
    double injection_probability = 0.1; 
    for (auto &B : F) 
    {
      // Set initial bug counts for this basic block
      for ( auto bug_type : config.bugs )
      {
        bb_to_bugcounts[bb_idx][bug_type.first] = 0;
      }
      
      // Loop over bug types that we may inject
      for ( auto bug_type : config.bugs )
      {
        std::string bug_name = bug_type.first;
        // Loop over instructions. Effectively, these are the "positions" 
        // where our bugs may be injected. 
        for ( auto &I : B )
        {
          // Check whether it is legal to inject a bug of this type here
          if ( legalToInject(F, bb_idx, bug_name) ) {
            IRBuilder<> builder(&I);
            // Construct the args for the bug function
            std::vector<Value*> args;
            for ( auto arg : config.bugs.at(bug_name).bug_function_args )
            {
              args.push_back( builder.getInt32( arg ) );
            }
            // Lookup bug function
            Constant* bugFunction = config.bugs.at(bug_name).bug_function;
            // Actually insert the bug function instructions
            ArrayRef<Value*> argsRef(args);
            builder.CreateCall( bugFunction, argsRef );
            // Update bug counts
            func_to_bugcounts[F.getName().str()][bug_name]++;
            bb_to_bugcounts[bb_idx][bug_name]++; 
#ifdef DEBUG
            errs() << "Error of type: " << bug_name 
                   << ", injected at function: " << F.getName() 
                   << ", basic block: " << bb_idx 
                   << ", instruction: " << in_idx << "\n"; 
#endif
          }
          in_idx++;
        }
      }
      bb_idx++;
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
