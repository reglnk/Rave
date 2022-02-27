module parser.gen;

import llvm;
import std.stdio : writeln;
import core.stdc.stdlib : exit;
import parser.typesystem, parser.util;
import parser.ast;
import std.conv : to;
import std.range;
import std.string;

class GStack {
    private LLVMValueRef[string] globals; // Global variables
    private LLVMValueRef[string] locals; // Local variables

    this() {}

    void addGlobal(LLVMValueRef v, string n) {
        globals[n] = v;
    }
    
    void addLocal(LLVMValueRef v, string n) {
		locals[n] = v;
	}

	bool isVariable(string var){return var in globals || var in locals;}
	bool isGlobal(string var) {return var in globals;}
	bool isLocal(string var) {return var in locals;}

	void remove(string n) {
		if(n in locals) locals.remove(n);
		else if(n in globals) globals.remove(n);
		else {}
	}

	void clean() {locals.clear();}

	LLVMValueRef opIndex(string n)
	{
		if(n in locals) return locals[n];
		else if(n in globals) return globals[n];
		else return null;
	}
}

class GenerationContext {
    LLVMModuleRef mod;
	AtstTypeContext typecontext;
	LLVMValueRef[string] global_vars;
	LLVMBuilderRef currbuilder;
	GStack gstack;

    this() {
        mod = LLVMModuleCreateWithName(cast(const char*)"epl");
		typecontext = new AtstTypeContext();
		gstack = new GStack();

		// Initialization
		LLVMInitializeAllTargets();
		LLVMInitializeAllAsmParsers();
		LLVMInitializeAllAsmPrinters();
		LLVMInitializeAllTargetInfos();
		LLVMInitializeAllTargetMCs();

		typecontext.types["int"] = new TypeBasic(BasicType.t_int);
		typecontext.types["short"] = new TypeBasic(BasicType.t_short);
		typecontext.types["char"] = new TypeBasic(BasicType.t_char);
		typecontext.types["long"] = new TypeBasic(BasicType.t_long);
    }

	string mangleQualifiedName(string[] parts, bool isFunction) {
		string o = "_EPL" ~ (isFunction ? "f" : "g");
		foreach(part; parts) {
			o ~= to!string(part.length) ~ part;
		}
		return o;
	}

	LLVMTypeRef getLLVMType(Type t) {
		if(auto tb = t.instanceof!TypeBasic) {
			switch(tb.basic) {
				case BasicType.t_int:
					return LLVMInt32Type();
				case BasicType.t_short:
					return LLVMInt16Type();
				case BasicType.t_long:
					return LLVMInt64Type();
				case BasicType.t_char:
					return LLVMInt8Type();
				case BasicType.t_float:
					return LLVMFloatType();
				default: return LLVMInt8Type();
			}
		}
		else if(auto p = t.instanceof!TypePointer) {
			return LLVMPointerType(getLLVMType(p.to), 0);
		}
		return LLVMInt32Type();
	}

    void gen(AstNode[] nodes, string file, bool debugMode) {
		for(int i=0; i<nodes.length; i++) {
			nodes[i].gen(this);
		}
		genTarget(file, debugMode);
	}

	void genTarget(string file,bool d) {
		if(d) 
			LLVMWriteBitcodeToFile(this.mod,cast(const char*)("bin/"~file~".debug.be"));

		char* errors;
		LLVMTargetRef target;
    	LLVMGetTargetFromTriple(LLVMGetDefaultTargetTriple(), &target, &errors);

    	LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
			target, 
			LLVMGetDefaultTargetTriple(), 
			"generic", 
			LLVMGetHostCPUFeatures(),
			 LLVMCodeGenLevelDefault, 
			 LLVMRelocDefault, 
		LLVMCodeModelDefault);

		LLVMDisposeMessage(errors);

    	LLVMSetTarget(this.mod, LLVMGetDefaultTargetTriple());
    	LLVMTargetDataRef datalayout = LLVMCreateTargetDataLayout(machine);
    	char* datalayout_str = LLVMCopyStringRepOfTargetData(datalayout);
    	LLVMSetDataLayout(this.mod, datalayout_str);
    	LLVMDisposeMessage(datalayout_str);
		char* file_ptr = cast(char*)toStringz(file);
		char* file_debug_ptr = cast(char*)toStringz("bin/"~file);

		char* err;
		LLVMPrintModuleToFile(this.mod, "tmp.ll", &err);

    	if(!d) LLVMTargetMachineEmitToFile(machine,this.mod,file_ptr, LLVMObjectFile, &errors);
		else LLVMTargetMachineEmitToFile(machine,this.mod,file_debug_ptr, LLVMObjectFile, &errors);
	}
}