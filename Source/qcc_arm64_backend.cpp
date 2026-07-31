//============================================================================
// qcc_arm64_backend.cpp -- QCC Stack-IR -> ARM64/Darwin-Assembler
//
// Eigenstaendiges Architecture Backend. start.s stellt die Plattform-Schicht
// (_start, tc_putint, tc_exit); dieser Generator emittiert nur Programmcode
// und Globals. Alle Operanden belegen aus Einfachheitsgruenden 16 Stackbytes.
//============================================================================
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Instr { std::string op; std::vector<std::string> args; int line; };
struct Function { std::string name; int nargs, first, last, locals, frameBytes; };
struct Global { std::string name; int initialValue; bool isChar, isPointer, isArray; int length; std::vector<int> init; };

static std::string trim(const std::string& s) {
	const std::string ws = " \t\r\n";
	const size_t a = s.find_first_not_of(ws);
	return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(ws) - a + 1);
}
static int number(const std::string& s, int line) {
	try { size_t n = 0; int v = std::stoi(s, &n); if (n != s.size()) throw 0; return v; }
	catch (...) { throw std::runtime_error("IR Zeile " + std::to_string(line) + ": Zahl erwartet"); }
}
static std::vector<Instr> readIR(const char* path) {
	std::ifstream in(path); std::vector<Instr> r; std::string raw; int line = 0;
	if (!in) throw std::runtime_error(std::string("kann IR nicht lesen: ") + path);
	while (std::getline(in, raw)) {
		std::istringstream words(trim(raw)); Instr x; x.line = ++line;
		if (!(words >> x.op) || x.op[0] == ';' || x.op[0] == '#' || x.op == "OK" || x.op == "FAIL") continue;
		std::string a; while (words >> a) x.args.push_back(a); r.push_back(x);
	}
	return r;
}
static std::vector<Global> globals(const std::vector<Instr>& ir) {
	std::vector<Global> r; std::map<std::string, bool> seen; bool funcs = false;
	for (const Instr& x : ir) {
		if (x.op == "FUNC") funcs = true;
		if (x.op == "GINIT") {
			bool found = false;
			if (funcs || x.args.size() != 3) throw std::runtime_error("ungueltiges GINIT");
			for (Global& g : r) if (g.name == x.args[0] && g.isArray) {
				int index = number(x.args[1], x.line); if (index < 0 || index >= g.length) throw std::runtime_error("GINIT-Index ausserhalb Array");
				g.init[index] = number(x.args[2], x.line); if (g.isChar) g.init[index] &= 255; found = true; break;
			}
			if (!found) throw std::runtime_error("GINIT fuer unbekanntes Array");
			continue;
		}
		if (x.op != "GLOBAL" && x.op != "GARRAY") continue;
		if (funcs || seen[x.args[0]])
			throw std::runtime_error("IR Zeile " + std::to_string(x.line) + ": ungueltiges GLOBAL");
		if (x.op == "GARRAY") {
			if (x.args.size() != 3 || (x.args[1] != "i" && x.args[1] != "u" && x.args[1] != "c" && x.args[1] != "b" && x.args[1] != "p")) throw std::runtime_error("ungueltiges GARRAY");
			int length = number(x.args[2], x.line); if (length <= 0) throw std::runtime_error("GARRAY-Laenge muss positiv sein");
			seen[x.args[0]] = true; r.push_back({x.args[0], 0, x.args[1] == "c" || x.args[1] == "b", x.args[1] == "p", true, length, std::vector<int>(length)}); continue;
		}
		if (x.args.size() != 1 && x.args.size() != 2 && x.args.size() != 3) throw std::runtime_error("ungueltiges GLOBAL");
		bool isChar = x.args.size() == 3 && (x.args[2] == "c" || x.args[2] == "b");
		if (x.args.size() == 3 && x.args[2] != "i" && x.args[2] != "u" && x.args[2] != "c" && x.args[2] != "b" && x.args[2] != "p") throw std::runtime_error("unbekannter Globaltyp");
		seen[x.args[0]] = true; r.push_back({x.args[0], x.args.size() >= 2 ? number(x.args[1], x.line) : 0, isChar, x.args.size() == 3 && x.args[2] == "p", false, 1, {}});
	}
	return r;
}
static std::vector<Function> functions(const std::vector<Instr>& ir) {
	std::vector<Function> r; bool open = false, seen = false; Function f{};
	for (size_t i = 0; i < ir.size(); ++i) {
		const Instr& x = ir[i];
		if (x.op == "GLOBAL" || x.op == "GARRAY" || x.op == "GINIT") { if (open || seen) throw std::runtime_error("ungueltiges GLOBAL"); }
		else if (x.op == "FUNC") {
			if (open || x.args.size() != 2) throw std::runtime_error("ungueltiges FUNC");
			f = {x.args[0], number(x.args[1], x.line), (int)i + 1, -1, 0, 0}; open = seen = true;
		} else if (x.op == "ENDFUNC") {
			if (!open) throw std::runtime_error("ENDFUNC ohne FUNC"); f.last = (int)i; r.push_back(f); open = false;
		} else if (!open) throw std::runtime_error("Opcode ausserhalb einer Funktion");
	}
	if (open || r.empty()) throw std::runtime_error("unvollstaendige IR");
	for (Function& f : r) {
		int top = f.nargs - 1;
		for (int i = f.first; i < f.last; ++i)
			if ((ir[i].op == "LOADL" || ir[i].op == "STOREL" || ir[i].op == "LOADC" || ir[i].op == "STOREC" || ir[i].op == "LOADP" || ir[i].op == "STOREP" || ir[i].op == "ADDRL" || ir[i].op == "LARRAY") && !ir[i].args.empty())
				top = std::max(top, number(ir[i].args[0], ir[i].line));
		f.locals = top >= f.nargs ? top - f.nargs + 1 : 0;
		int bytes = f.locals * 16;
		for (int i = f.first; i < f.last; ++i) if (ir[i].op == "LARRAY") {
			if (ir[i].args.size() != 3 || (ir[i].args[1] != "i" && ir[i].args[1] != "c" && ir[i].args[1] != "b" && ir[i].args[1] != "p")) throw std::runtime_error("ungueltiges LARRAY");
			int len = number(ir[i].args[2], ir[i].line); if (len <= 0) throw std::runtime_error("LARRAY-Laenge muss positiv sein");
			int align = ir[i].args[1] == "p" ? 8 : ir[i].args[1] == "c" || ir[i].args[1] == "b" ? 1 : 4;
			bytes = (bytes + align - 1) & ~(align - 1);
			bytes += len * align;
		}
		f.frameBytes = (bytes + 15) & ~15;
	}
	return r;
}
static int arrayOffset(const std::vector<Instr>& ir, const Function& f, int wanted, bool& isChar, int line) {
	int offset = f.locals * 16;
	for (int i = f.first; i < f.last; ++i) if (ir[i].op == "LARRAY") {
		const Instr& x = ir[i]; if (x.args.size() != 3) throw std::runtime_error("ungueltiges LARRAY");
		int slot = number(x.args[0], x.line), len = number(x.args[2], x.line); int align = x.args[1] == "p" ? 8 : x.args[1] == "c" || x.args[1] == "b" ? 1 : 4;
		offset = (offset + align - 1) & ~(align - 1); offset += len * align;
		if (slot == wanted) { isChar = x.args[1] == "c" || x.args[1] == "b"; return offset; }
	}
	throw std::runtime_error("IR Zeile " + std::to_string(line) + ": unbekanntes lokales Array");
}
static std::string slot(int n, const Function& f, int line) {
	if (n < 0 || n >= f.nargs + f.locals) throw std::runtime_error("IR Zeile " + std::to_string(line) + ": Slot ausserhalb Frame");
	return n < f.nargs ? "#" + std::to_string(16 + 16 * (f.nargs - 1 - n))
	                   : "#-" + std::to_string(16 * (n - f.nargs + 1));
}
static void push(std::ostream& o, const char* reg = "w0") { o << "\tstr\t" << reg << ",[sp,#-16]!\n"; }
static void pop(std::ostream& o, const char* reg = "w0") { o << "\tldr\t" << reg << ",[sp]\n\tadd\tsp,sp,#16\n"; }
static void pushPointer(std::ostream& o, const char* reg = "x0") { o << "\tstr\t" << reg << ",[sp,#-16]!\n"; }
static void popPointer(std::ostream& o, const char* reg = "x0") { o << "\tldr\t" << reg << ",[sp]\n\tadd\tsp,sp,#16\n"; }
static void emit(std::ostream& o, const std::vector<Instr>& ir, const std::vector<Function>& fs, const std::vector<Global>& gs) {
	std::map<std::string, Function> fn; std::map<std::string, bool> global;
	for (const Function& f : fs) fn[f.name] = f;
	for (const Global& g : gs) global[g.name] = true;
	if (!fn.count("main")) throw std::runtime_error("IR: Funktion main fehlt");
	o << "; QCC ARM64/Darwin -- PIC Programmmodul\n\t.text\n\t.p2align\t2\n";
	for (const Function& f : fs) {
		o << "\t.globl\t_tc_" << f.name << "\n_tc_" << f.name << ":\n\tstp\tx29,x30,[sp,#-16]!\n\tmov\tx29,sp\n";
		if (f.frameBytes) o << "\tsub\tsp,sp,#" << f.frameBytes << "\n";
		for (int i = f.first; i < f.last; ++i) {
			const Instr& x = ir[i]; const std::string& op = x.op;
			if (op == "PUSH" && x.args.size() == 1) { o << "\tmov\tw0,#" << x.args[0] << "\n"; push(o); }
			else if (op == "LOADL" && x.args.size() == 1) { o << "\tldr\tw0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; push(o); }
			else if (op == "STOREL" && x.args.size() == 1) { pop(o); o << "\tstr\tw0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; }
			else if (op == "LOADC" && x.args.size() == 1) { o << "\tldrb\tw0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; push(o); }
			else if (op == "STOREC" && x.args.size() == 1) { pop(o); o << "\tstrb\tw0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; }
			else if (op == "LOADP" && x.args.size() == 1) { o << "\tldr\tx0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; pushPointer(o); }
			else if (op == "STOREP" && x.args.size() == 1) { popPointer(o); o << "\tstr\tx0,[x29," << slot(number(x.args[0], x.line), f, x.line) << "]\n"; }
			else if (op == "ADDRL" && x.args.size() == 1) {
				int n = number(x.args[0], x.line);
				if (n < f.nargs) o << "\tadd\tx0,x29,#" << (16 + 16 * (f.nargs - 1 - n)) << "\n";
				else o << "\tsub\tx0,x29,#" << (16 * (n - f.nargs + 1)) << "\n";
				pushPointer(o);
			}
			else if (op == "ADDRG" && x.args.size() == 1) {
				if (!global.count(x.args[0])) throw std::runtime_error("unbekannte globale Variable");
				o << "\tadrp\tx0,_tc_g_" << x.args[0] << "@PAGE\n\tadd\tx0,x0,_tc_g_" << x.args[0] << "@PAGEOFF\n"; pushPointer(o);
			}
			else if (op == "LARRAY" && x.args.size() == 3) { }
			else if (op == "PUSHADDR" && x.args.size() == 2) {
				if (x.args[0] == "L") { bool ignored = false; int off = arrayOffset(ir, f, number(x.args[1], x.line), ignored, x.line); o << "\tsub\tx0,x29,#" << off << "\n"; }
				else if (x.args[0] == "P") o << "\tldr\tx0,[x29," << slot(number(x.args[1], x.line), f, x.line) << "]\n";
				else if (x.args[0] == "G" && global.count(x.args[1])) o << "\tadrp\tx0,_tc_g_" << x.args[1] << "@PAGE\n\tadd\tx0,x0,_tc_g_" << x.args[1] << "@PAGEOFF\n";
				else throw std::runtime_error("unbekanntes Array");
				o << "\tstr\tx0,[sp,#-16]!\n";
			}
			else if ((op == "LOADIDX" || op == "STOREIDX") && x.args.size() == 3) {
				bool isChar = x.args[2] == "c" || x.args[2] == "b", isPointer = x.args[2] == "p"; if (x.args[2] != "i" && !isChar && !isPointer) throw std::runtime_error("unbekannter Arraytyp");
				if (op == "STOREIDX") { if (isPointer) popPointer(o, "x0"); else pop(o, "w0"); } pop(o, "w1");
				if (x.args[0] == "L") { int off = arrayOffset(ir, f, number(x.args[1], x.line), isChar, x.line); o << "\tsub\tx9,x29,#" << off << "\n"; }
				else if (x.args[0] == "P") o << "\tldr\tx9,[x29," << slot(number(x.args[1], x.line), f, x.line) << "]\n";
				else if (x.args[0] == "G" && global.count(x.args[1])) o << "\tadrp\tx9,_tc_g_" << x.args[1] << "@PAGE\n\tadd\tx9,x9,_tc_g_" << x.args[1] << "@PAGEOFF\n";
				else throw std::runtime_error("unbekanntes Array");
				o << "\tadd\tx9,x9,w1,sxtw" << (isChar ? "\n" : isPointer ? " #3\n" : " #2\n");
				if (op == "LOADIDX") { o << "\tldr" << (isChar ? "b" : "") << "\t" << (isPointer ? "x0" : "w0") << ",[x9]\n"; if (isPointer) pushPointer(o); else push(o); }
				else o << "\tstr" << (isChar ? "b" : "") << "\t" << (isPointer ? "x0" : "w0") << ",[x9]\n";
			}
			else if ((op == "LOADG" || op == "STOREG") && x.args.size() == 1) {
				if (!global.count(x.args[0])) throw std::runtime_error("unbekannte globale Variable");
				o << "\tadrp\tx9,_tc_g_" << x.args[0] << "@PAGE\n";
				if (op == "LOADG") { o << "\tldr\tw0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; push(o); }
				else { pop(o); o << "\tstr\tw0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; }
			}
			else if ((op == "LOADGC" || op == "STOREGC") && x.args.size() == 1) {
				if (!global.count(x.args[0])) throw std::runtime_error("unbekannte globale Variable");
				o << "\tadrp\tx9,_tc_g_" << x.args[0] << "@PAGE\n";
				if (op == "LOADGC") { o << "\tldrb\tw0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; push(o); }
				else { pop(o); o << "\tstrb\tw0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; }
			}
			else if ((op == "LOADGP" || op == "STOREGP") && x.args.size() == 1) {
				if (!global.count(x.args[0])) throw std::runtime_error("unbekannte globale Variable");
				o << "\tadrp\tx9,_tc_g_" << x.args[0] << "@PAGE\n";
				if (op == "LOADGP") { o << "\tldr\tx0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; pushPointer(o); }
				else { popPointer(o); o << "\tstr\tx0,[x9,_tc_g_" << x.args[0] << "@PAGEOFF]\n"; }
			}
			else if (op == "PTRINDEX" && x.args.size() == 1) {
				popPointer(o, "x0"); pop(o, "w1");
				o << "\tadd\tx0,x0,w1,sxtw" << (x.args[0] == "c" || x.args[0] == "b" ? "\n" : x.args[0] == "p" ? " #3\n" : " #2\n"); pushPointer(o);
			}
			else if ((op == "LOADIND" || op == "STOREIND") && x.args.size() == 1) {
				bool byte = x.args[0] == "c" || x.args[0] == "b", ptr = x.args[0] == "p";
				if (op == "LOADIND") { popPointer(o, "x9"); o << "\tldr" << (byte ? "b" : "") << "\t" << (ptr ? "x0" : "w0") << ",[x9]\n"; if (ptr) pushPointer(o); else push(o); }
				else { if (ptr) popPointer(o, "x0"); else pop(o, "w0"); popPointer(o, "x9"); o << "\tstr" << (byte ? "b" : "") << "\t" << (ptr ? "x0" : "w0") << ",[x9]\n"; }
			}
			else if ((op == "PADD" || op == "PSUB") && x.args.size() == 1) {
				pop(o, "w1"); popPointer(o, "x0"); const char* sign = op == "PADD" ? "add" : "sub";
				o << "\t" << sign << "\tx0,x0,w1,sxtw" << (x.args[0] == "c" || x.args[0] == "b" ? "\n" : x.args[0] == "p" ? " #3\n" : " #2\n"); pushPointer(o);
			}
			else if (op == "IPADD" && x.args.size() == 1) {
				popPointer(o, "x0"); pop(o, "w1"); o << "\tadd\tx0,x0,w1,sxtw" << (x.args[0] == "c" || x.args[0] == "b" ? "\n" : x.args[0] == "p" ? " #3\n" : " #2\n"); pushPointer(o);
			}
			else if (op == "PDIFF" && x.args.size() == 1) {
				popPointer(o, "x1"); popPointer(o, "x0"); o << "\tsub\tx0,x0,x1\n";
				if (x.args[0] != "c" && x.args[0] != "b") o << "\tasr\tx0,x0,#" << (x.args[0] == "p" ? 3 : 2) << "\n";
				push(o);
			}
			else if (op == "ADD" || op == "SUB" || op == "MUL" || op == "DIV" || op == "UDIV") {
				pop(o, "w1"); pop(o, "w0");
				o << "\t" << (op == "ADD" ? "add" : op == "SUB" ? "sub" : op == "MUL" ? "mul" : op == "DIV" ? "sdiv" : "udiv") << "\tw0,w0,w1\n"; push(o);
			}
			else if (op == "MOD" || op == "UMOD") {
				pop(o, "w1"); pop(o, "w0");
				o << "\t" << (op == "MOD" ? "sdiv" : "udiv") << "\tw2,w0,w1\n\tmsub\tw0,w2,w1,w0\n"; push(o);
			}
			else if (op == "NEG") { pop(o); o << "\tneg\tw0,w0\n"; push(o); }
			else if (op == "NOT") { pop(o); o << "\tcmp\tw0,#0\n\tcset\tw0,eq\n"; push(o); }
			else if (op == "NOTBIT") { pop(o); o << "\tmvn\tw0,w0\n"; push(o); }
			else if (op == "BAND" || op == "BXOR" || op == "BOR") {
				pop(o); o << "\tmov\tw1,w0\n"; pop(o);
				o << "\t" << (op == "BAND" ? "and" : op == "BXOR" ? "eor" : "orr") << "\tw0,w0,w1\n"; push(o);
			}
			else if (op == "SHL" || op == "SHR" || op == "USHR") {
				pop(o); o << "\tmov\tw1,w0\n"; pop(o);
				o << "\t" << (op == "SHL" ? "lslv" : op == "SHR" ? "asrv" : "lsrv") << "\tw0,w0,w1\n"; push(o);
			}
			else if (op == "NARROWC") { pop(o); o << "\tand\tw0,w0,#255\n"; push(o); }
			else if (op == "DUP") { o << "\tldr\tw0,[sp]\n"; push(o); }
			else if (op == "DUPP") { o << "\tldr\tx0,[sp]\n"; pushPointer(o); }
			else if (op.rfind("CMP", 0) == 0) {
				const char* cc = op == "CMPLT" ? "lt" : op == "CMPGT" ? "gt" : op == "CMPLE" ? "le" : op == "CMPGE" ? "ge" : op == "CMPULT" ? "lo" : op == "CMPUGT" ? "hi" : op == "CMPULE" ? "ls" : op == "CMPUGE" ? "hs" : op == "CMPEQ" ? "eq" : op == "CMPNE" ? "ne" : nullptr;
				if (!cc) throw std::runtime_error("unbekannter Vergleich");
				pop(o, "w1"); pop(o, "w0"); o << "\tcmp\tw0,w1\n\tcset\tw0," << cc << "\n"; push(o);
			}
			else if (op.rfind("PCMP", 0) == 0) {
				const char* cc = op == "PCMPLT" ? "lo" : op == "PCMPGT" ? "hi" : op == "PCMPLE" ? "ls" : op == "PCMPGE" ? "hs" : op == "PCMPEQ" ? "eq" : op == "PCMPNE" ? "ne" : nullptr;
				if (!cc) throw std::runtime_error("unbekannter Pointervergleich");
				popPointer(o, "x1"); popPointer(o, "x0"); o << "\tcmp\tx0,x1\n\tcset\tw0," << cc << "\n"; push(o);
			}
			else if (op == "LABEL" && x.args.size() == 1) o << "_tc_" << x.args[0] << ":\n";
			else if (op == "JMP" && x.args.size() == 1) o << "\tb\t_tc_" << x.args[0] << "\n";
			else if ((op == "JZ" || op == "JNZ") && x.args.size() == 1) { pop(o); o << "\tcb" << (op == "JZ" ? "z" : "nz") << "\tw0,_tc_" << x.args[0] << "\n"; }
			else if ((op == "CALL" || op == "CALLP") && x.args.size() == 2) {
				int n = number(x.args[1], x.line); if (!fn.count(x.args[0])) throw std::runtime_error("unbekannte Funktion");
				o << "\tbl\t_tc_" << x.args[0] << "\n"; if (n) o << "\tadd\tsp,sp,#" << (n * 16) << "\n"; if (op == "CALLP") pushPointer(o); else push(o);
			}
			else if (op == "RET" || op == "RETP") { if (op == "RETP") popPointer(o); else pop(o); o << "\tmov\tsp,x29\n\tldp\tx29,x30,[sp],#16\n\tret\n"; }
			else if (op == "DROP") o << "\tadd\tsp,sp,#16\n";
			else if (op == "PRINT") { pop(o); o << "\tbl\t_tc_putint\n"; }
			else if (op == "PRINTU") { pop(o); o << "\tbl\t_tc_putuint\n"; }
			else if (op == "PRINTC") { pop(o); o << "\tbl\t_tc_putchar\n"; }
			else throw std::runtime_error("IR Zeile " + std::to_string(x.line) + ": unbekannter Opcode " + op);
		}
		o << "\n";
	}
	for (const Global& g : gs) if (g.initialValue == 0 && g.init.empty())
		o << "\t.zerofill\t__DATA,__bss,_tc_g_" << g.name << "," << ((g.isChar ? 1 : g.isPointer ? 8 : 4) * g.length) << "," << (g.isChar ? 0 : g.isPointer ? 3 : 2) << "\n";
	bool hasData = false;
	for (const Global& g : gs) hasData |= g.initialValue != 0 || !g.init.empty();
	if (hasData) {
		o << "\t.section\t__DATA,__data\n\t.p2align\t2\n";
		for (const Global& g : gs) if (g.initialValue != 0 || !g.init.empty()) {
			if (!g.isChar) o << "\t.p2align\t2\n";
			if (g.isPointer) o << "\t.p2align\t3\n";
			o << "_tc_g_" << g.name << ":\n";
			if (g.init.empty()) o << "\t." << (g.isChar ? "byte" : g.isPointer ? "quad" : "long") << "\t" << g.initialValue << "\n";
			else for (int value : g.init) o << "\t." << (g.isChar ? "byte" : g.isPointer ? "quad" : "long") << "\t" << value << "\n";
		}
	}
}
int main(int argc, char* argv[]) {
	try {
		if (argc != 3) { std::cerr << "usage: " << argv[0] << " <input.ir> <output.s>\n"; return 2; }
		auto ir = readIR(argv[1]); auto gs = globals(ir); auto fs = functions(ir); std::ofstream out(argv[2]);
		if (!out) throw std::runtime_error("kann Ausgabe nicht schreiben"); emit(out, ir, fs, gs);
	} catch (const std::exception& e) { std::cerr << "qcc_arm64_backend: " << e.what() << "\n"; return 1; }
	return 0;
}
