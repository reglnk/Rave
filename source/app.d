import std.stdio;
import lexer;
import tokens;
import std.conv : to;
import core.stdc.stdlib : exit;
import cmd;
import std.file : readText;
import preproc;
import parser;

void main(string[] args)
{
	input(args);
	auto lexer = new Lexer(readText(source_file));
	auto preproc = new Preprocessor(lexer.getTokens());
	auto parse = new Parser(preproc.getTokens());
	
	auto nodes = parse.parseProgram();
	
	for(int i = 0; i < nodes.length; ++i) {
		nodes[i].debugPrint(0);
	}

	// for(int i=0; i<preproc.getTokens().length; i++) {
	// 	TokType type = preproc.getTokens()[i].type;
	// 	string value = preproc.getTokens()[i].value;

	// 	if(type == TokType.tok_cmd) {
	// 		writeln("Type: "~to!string(preproc.getTokens()[i].cmd)~" "~value);
	// 	}
	// 	else {
	// 		writeln("Type: "~to!string(type)~" "~value);
	// 	}
	// }
}
