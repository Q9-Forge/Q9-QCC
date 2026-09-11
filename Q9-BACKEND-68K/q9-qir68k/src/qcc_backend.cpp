//=============================================================================
// qcc_backend.cpp -- Q9 Stack-IR to position-independent 68k assembly
//
// Purpose:
//   Reference C++ implementation of the Q9 68k backend. The generated output
//   uses position-independent calls and stack-frame-relative data access.
//
// Edition history:
//   2026-09-11  Introduced the English source-header format.
//=============================================================================
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Instr {
	std::string op;
	std::vector<std::string> args;
	int line;
};

struct Function {
	std::string name;
	int nargs;
	int first;
	int last;
	int locals;
	int frameBytes;
};

struct Global {
	std::string name;
	int initialValue;
	bool isChar;
	bool isArray;
	int length;
	std::vector<int> init;
};

static std::string trim(const std::string& in) {
	const std::string ws = " \t\r\n";
	const std::string::size_type first = in.find_first_not_of(ws);
	if (first == std::string::npos) return "";
	return in.substr(first, in.find_last_not_of(ws) - first + 1);
}

static int asInt(const std::string& text, int line) {
	try {
		size_t used = 0;
		int n = std::stoi(text, &used, 10);
		if (used != text.size()) throw std::invalid_argument("tail");
		return n;
	}
	catch (...) {
		throw std::runtime_error("IR Zeile " + std::to_string(line) + ": Zahl erwartet: " + text);
	}
}

static std::vector<Instr> readIR(const char* path) {
	std::ifstream file(path);
	std::vector<Instr> result;
	std::string raw;
	int line = 0;
	if (!file) throw std::runtime_error(std::string("kann IR nicht lesen: ") + path);
	while (std::getline(file, raw)) {
		std::istringstream words(trim(raw));
		Instr ins;
		ins.line = ++line;
		if (!(words >> ins.op) || ins.op[0] == ';' || ins.op[0] == '#') continue;
		if (ins.op == "OK" || ins.op == "FAIL") continue; // Meldung des generierten Frontends
		std::string arg;
		while (words >> arg) ins.args.push_back(arg);
		result.push_back(ins);
	}
	return result;
}

static std::vector<Global> findGlobals(const std::vector<Instr>& ir) {
	std::vector<Global> globals;
	std::map<std::string, bool> known;
	bool seenFunction = false;
	for (const Instr& ins : ir) {
		if (ins.op == "FUNC") seenFunction = true;
		if (ins.op == "GINIT") {
			bool found = false;
			if (seenFunction || ins.args.size() != 3) throw std::runtime_error("ungueltiges GINIT");
			for (Global& global : globals) if (global.name == ins.args[0] && global.isArray) {
				int index = asInt(ins.args[1], ins.line); if (index < 0 || index >= global.length) throw std::runtime_error("GINIT-Index ausserhalb Array");
				global.init[index] = asInt(ins.args[2], ins.line); if (global.isChar) global.init[index] &= 255; found = true; break;
			}
			if (!found) throw std::runtime_error("GINIT fuer unbekanntes Array");
			continue;
		}
		if (ins.op != "GLOBAL" && ins.op != "GARRAY") continue;
		if (ins.op == "GARRAY") {
			if (seenFunction || ins.args.size() != 3 || (ins.args[1] != "i" && ins.args[1] != "u" && ins.args[1] != "c" && ins.args[1] != "b" && ins.args[1] != "p")) throw std::runtime_error("ungueltiges GARRAY");
			if (known[ins.args[0]]) throw std::runtime_error("doppelte globale Variable");
			int len = asInt(ins.args[2], ins.line); if (len <= 0) throw std::runtime_error("GARRAY-Laenge muss positiv sein");
			known[ins.args[0]] = true; globals.push_back({ins.args[0], 0, ins.args[1] == "c" || ins.args[1] == "b", true, len, std::vector<int>(len)}); continue;
		}
		if (seenFunction || (ins.args.size() != 1 && ins.args.size() != 2 && ins.args.size() != 3))
			throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": GLOBAL muss genau einmal vor Funktionen stehen");
		if (known[ins.args[0]]) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": doppelte globale Variable " + ins.args[0]);
		known[ins.args[0]] = true;
		const bool isChar = ins.args.size() == 3 && (ins.args[2] == "c" || ins.args[2] == "b");
		if (ins.args.size() == 3 && ins.args[2] != "i" && ins.args[2] != "u" && ins.args[2] != "c" && ins.args[2] != "b" && ins.args[2] != "p") throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannter Globaltyp");
		globals.push_back({ ins.args[0], ins.args.size() >= 2 ? asInt(ins.args[1], ins.line) : 0, isChar, false, 1, {} });
	}
	return globals;
}

static std::vector<Function> findFunctions(const std::vector<Instr>& ir) {
	std::vector<Function> funcs;
	bool open = false;
	bool seenFunction = false;
	Function current;
	for (size_t i = 0; i < ir.size(); ++i) {
		const Instr& ins = ir[i];
		if (ins.op == "GLOBAL" || ins.op == "GARRAY" || ins.op == "GINIT") {
			if (open || seenFunction || (ins.args.size() != 1 && ins.args.size() != 2 && ins.args.size() != 3)) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": ungueltiges GLOBAL");
		}
		else if (ins.op == "FUNC") {
			if (open || ins.args.size() != 2) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": ungueltiges FUNC");
			current.name = ins.args[0];
			current.nargs = asInt(ins.args[1], ins.line);
			current.first = (int)i + 1;
			current.last = -1;
			current.locals = 0;
			current.frameBytes = 0;
			open = true;
			seenFunction = true;
		}
		else if (ins.op == "ENDFUNC") {
			if (!open) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": ENDFUNC ohne FUNC");
			current.last = (int)i;
			funcs.push_back(current);
			open = false;
		}
		else if (!open) {
			throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": Opcode ausserhalb einer Funktion");
		}
	}
	if (open) throw std::runtime_error("IR: fehlendes ENDFUNC");
	if (funcs.empty()) throw std::runtime_error("IR: keine Funktion");

	for (Function& fn : funcs) {
		int highest = fn.nargs - 1;
		for (int i = fn.first; i < fn.last; ++i) {
			const Instr& ins = ir[(size_t)i];
			if ((ins.op == "LOADL" || ins.op == "STOREL" || ins.op == "LOADC" || ins.op == "STOREC" || ins.op == "LOADP" || ins.op == "STOREP" || ins.op == "ADDRL" || ins.op == "LARRAY") && !ins.args.empty()) {
				const int slot = asInt(ins.args[0], ins.line);
				if (slot < 0) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": negativer lokaler Slot");
				if (slot > highest) highest = slot;
			}
		}
		fn.locals = highest >= fn.nargs ? highest - fn.nargs + 1 : 0;
		fn.frameBytes = fn.locals * 4;
		for (int i = fn.first; i < fn.last; ++i) if (ir[(size_t)i].op == "LARRAY") {
			const Instr& ins = ir[(size_t)i]; if (ins.args.size() != 3) throw std::runtime_error("ungueltiges LARRAY");
			int len = asInt(ins.args[2], ins.line); if (len <= 0) throw std::runtime_error("LARRAY-Laenge muss positiv sein");
			int align = ins.args[1] == "c" || ins.args[1] == "b" ? 1 : 2;
			fn.frameBytes = (fn.frameBytes + align - 1) & ~(align - 1);
			fn.frameBytes += len * (ins.args[1] == "c" || ins.args[1] == "b" ? 1 : 4);
		}
		fn.frameBytes = (fn.frameBytes + 3) & ~3;
	}
	return funcs;
}
static int arrayOffset(const std::vector<Instr>& ir, const Function& fn, int wanted, bool& isChar, int line) {
	int offset = fn.locals * 4;
	for (int i = fn.first; i < fn.last; ++i) if (ir[(size_t)i].op == "LARRAY") {
		const Instr& x = ir[(size_t)i]; if (x.args.size() != 3) throw std::runtime_error("ungueltiges LARRAY");
		int slot = asInt(x.args[0], x.line), len = asInt(x.args[2], x.line), align = x.args[1] == "c" || x.args[1] == "b" ? 1 : 2;
		offset = (offset + align - 1) & ~(align - 1); offset += len * (x.args[1] == "c" || x.args[1] == "b" ? 1 : 4);
		if (slot == wanted) { isChar = x.args[1] == "c" || x.args[1] == "b"; return offset; }
	}
	throw std::runtime_error("IR Zeile " + std::to_string(line) + ": unbekanntes lokales Array");
}

static std::string slotAddress(int slot, const Function& fn, int line) {
	if (slot < fn.nargs) {
		// With left-to-right argument pushes, parameter 0 is at the top.
		return std::to_string(8 + 4 * (fn.nargs - 1 - slot)) + "(a6)";
	}
	if (slot >= fn.nargs + fn.locals) throw std::runtime_error("IR Zeile " + std::to_string(line) + ": Slot ausserhalb des Frames");
	return std::to_string(-4 * (slot - fn.nargs + 1)) + "(a6)";
}

static void emitCompare(std::ostream& out, const std::string& branch, int& serial) {
	const std::string yes = "tc_cmp_yes_" + std::to_string(serial);
	const std::string done = "tc_cmp_done_" + std::to_string(serial++);
	out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tcmp.l\td1,d0\n\tmoveq\t#0,d0\n";
	out << "\t" << branch << "\t" << yes << "\n\tbra\t" << done << "\n";
	out << yes << ":\tmoveq\t#1,d0\n" << done << ":\tmove.l\td0,-(a7)\n";
}

// The 68000 MULS/DIVS instructions support only 16-bit operands. These fixed,
// PIC-friendly templates implement the defined QCC int32 arithmetic. They
// preserve d2-d5 (ABI-friendly) and return only in d0.
static void emitM68kCore(std::ostream& out) {
	out << "; 68k-Core: int32 MUL/DIV, keine OS- oder Q9-Abhaengigkeit\n";
	out << "tc_mul_i32:\n";
	out << "\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n";
	out << "\tmoveq\t#0,d2\n\tmoveq\t#0,d4\n\ttst.l\td0\n\tbpl\ttc_mul_a_pos\n";
	out << "\tneg.l\td0\n\taddq.l\t#1,d4\n";
	out << "tc_mul_a_pos:\ttst.l\td1\n\tbpl\ttc_mul_b_pos\n\tneg.l\td1\n\teori.l\t#1,d4\n";
	out << "tc_mul_b_pos:\tmoveq\t#31,d3\n";
	out << "tc_mul_loop:\tlsr.l\t#1,d1\n\tbcc\ttc_mul_skip\n\tadd.l\td0,d2\n";
	out << "tc_mul_skip:\tadd.l\td0,d0\n\tdbra\td3,tc_mul_loop\n";
	out << "\ttst.l\td4\n\tbeq\ttc_mul_done\n\tneg.l\td2\n";
	out << "tc_mul_done:\tmove.l\td2,d0\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n";

	out << "tc_div_i32:\n";
	out << "\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n\tmove.l\td5,-(a7)\n";
	out << "\tmoveq\t#0,d5\n\ttst.l\td1\n\tbne\ttc_div_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_div_done\n";
	out << "tc_div_nonzero:\ttst.l\td0\n\tbpl\ttc_div_a_pos\n\tneg.l\td0\n\taddq.l\t#1,d5\n";
	out << "tc_div_a_pos:\ttst.l\td1\n\tbpl\ttc_div_b_pos\n\tneg.l\td1\n\teori.l\t#1,d5\n";
	out << "tc_div_b_pos:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n";
	out << "tc_div_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n";
	out << "\tcmp.l\td1,d3\n\tbcs\ttc_div_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n";
	out << "tc_div_skip:\tdbra\td4,tc_div_loop\n\ttst.l\td5\n\tbeq\ttc_div_result\n\tneg.l\td2\n";
	out << "tc_div_result:\tmove.l\td2,d0\n";
	out << "tc_div_done:\tmove.l\t(a7)+,d5\n\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n";

	out << "tc_udiv_u32:\n";
	out << "\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td4,-(a7)\n";
	out << "\ttst.l\td1\n\tbne\ttc_udiv_nonzero\n\tmoveq\t#0,d0\n\tbra\ttc_udiv_done\n";
	out << "tc_udiv_nonzero:\tmoveq\t#0,d2\n\tmoveq\t#0,d3\n\tmoveq\t#31,d4\n";
	out << "tc_udiv_loop:\tlsl.l\t#1,d0\n\troxl.l\t#1,d3\n\tlsl.l\t#1,d2\n";
	out << "\tcmp.l\td1,d3\n\tbcs\ttc_udiv_skip\n\tsub.l\td1,d3\n\taddq.l\t#1,d2\n";
	out << "tc_udiv_skip:\tdbra\td4,tc_udiv_loop\n\tmove.l\td2,d0\n";
	out << "tc_udiv_done:\tmove.l\t(a7)+,d4\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n";

	out << "tc_mod_i32:\n";
	out << "\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_div_i32\n\tmove.l\td0,d1\n\tmove.l\td2,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n";
	out << "tc_umod_u32:\n";
	out << "\tmove.l\td2,-(a7)\n\tmove.l\td3,-(a7)\n\tmove.l\td0,d2\n\tmove.l\td1,d3\n\tbsr\ttc_udiv_u32\n\tmove.l\td0,d1\n\tmove.l\td2,d0\n\tbsr\ttc_mul_i32\n\tsub.l\td0,d2\n\tmove.l\td2,d0\n\tmove.l\t(a7)+,d3\n\tmove.l\t(a7)+,d2\n\trts\n\n";
}

static void emitIR(std::ostream& out, const std::vector<Instr>& ir, const std::vector<Function>& funcs, const std::vector<Global>& globals) {
	std::map<std::string, Function> byName;
	std::map<std::string, Global> globalByName;
	int serial = 0;
	for (const Function& fn : funcs) byName[fn.name] = fn;
	for (const Global& global : globals) globalByName[global.name] = global;
	if (byName.find("main") == byName.end()) throw std::runtime_error("IR: Funktion main fehlt");

	out << "; QCC 68k backend -- PIC Einzelmodul, erzeugt aus Stack-IR\n";
	out << "; a7: Operand-Stack, a6: aktueller Frame, d0/d1: Scratch/Rueckgabe\n\n";
	out << "tc_start:\tbsr\ttc_main\n\tbra\ttc_exit\n\n";
	for (const Function& fn : funcs) {
		out << "tc_" << fn.name << ":\tlink\ta6,#" << -fn.frameBytes << "\n";
		for (int i = fn.first; i < fn.last; ++i) {
			const Instr& ins = ir[(size_t)i];
			const std::string& op = ins.op;
			if (op == "PUSH" && ins.args.size() == 1) out << "\tmove.l\t#" << ins.args[0] << ",-(a7)\n";
			else if (op == "LOADL" && ins.args.size() == 1) out << "\tmove.l\t" << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << ",-(a7)\n";
			else if (op == "STOREL" && ins.args.size() == 1) out << "\tmove.l\t(a7)+," << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << "\n";
			else if (op == "LOADC" && ins.args.size() == 1) out << "\tmoveq\t#0,d0\n\tmove.b\t" << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << ",d0\n\tmove.l\td0,-(a7)\n";
			else if (op == "STOREC" && ins.args.size() == 1) out << "\tmove.l\t(a7)+,d0\n\tmove.b\td0," << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << "\n";
			else if (op == "LOADP" && ins.args.size() == 1) out << "\tmove.l\t" << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << ",-(a7)\n";
			else if (op == "STOREP" && ins.args.size() == 1) out << "\tmove.l\t(a7)+," << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << "\n";
			else if (op == "ADDRL" && ins.args.size() == 1) out << "\tlea\t" << slotAddress(asInt(ins.args[0], ins.line), fn, ins.line) << ",a0\n\tmove.l\ta0,-(a7)\n";
			else if (op == "ADDRG" && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("unbekannte globale Variable");
				out << "\tlea\ttc_g_" << ins.args[0] << "(pc),a0\n\tmove.l\ta0,-(a7)\n";
			}
			else if (op == "LARRAY" && ins.args.size() == 3) { }
			else if (op == "PUSHADDR" && ins.args.size() == 2) {
				if (ins.args[0] == "L") {
					int slot = asInt(ins.args[1], ins.line);
					if (slot < fn.nargs) out << "\tlea\t" << slotAddress(slot, fn, ins.line) << ",a0\n";
					else { bool ignored = false; int off = arrayOffset(ir, fn, slot, ignored, ins.line); out << "\tlea\t-" << off << "(a6),a0\n"; }
				}
				else if (ins.args[0] == "P") out << "\tmove.l\t" << slotAddress(asInt(ins.args[1], ins.line), fn, ins.line) << ",a0\n";
				else if (ins.args[0] == "G" && globalByName.find(ins.args[1]) != globalByName.end()) out << "\tlea\ttc_g_" << ins.args[1] << "(pc),a0\n";
				else throw std::runtime_error("unbekanntes Array");
				out << "\tmove.l\ta0,-(a7)\n";
			}
			else if ((op == "LOADIDX" || op == "STOREIDX") && ins.args.size() == 3) {
				bool isChar = ins.args[2] == "c" || ins.args[2] == "b"; if (ins.args[2] != "i" && ins.args[2] != "p" && !isChar) throw std::runtime_error("unbekannter Arraytyp");
				if (op == "STOREIDX") out << "\tmove.l\t(a7)+,d0\n";
				out << "\tmove.l\t(a7)+,d1\n";
				if (!isChar) out << "\tlsl.l\t#2,d1\n";
				if (ins.args[0] == "L") { int off = arrayOffset(ir, fn, asInt(ins.args[1], ins.line), isChar, ins.line); out << "\tlea\t-" << off << "(a6),a0\n"; }
				else if (ins.args[0] == "P") out << "\tmove.l\t" << slotAddress(asInt(ins.args[1], ins.line), fn, ins.line) << ",a0\n";
				else if (ins.args[0] == "G" && globalByName.find(ins.args[1]) != globalByName.end()) out << "\tlea\ttc_g_" << ins.args[1] << "(pc),a0\n";
				else throw std::runtime_error("unbekanntes Array");
				out << "\tadd.l\td1,a0\n";
				if (op == "LOADIDX") { if (isChar) out << "\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n"; else out << "\tmove.l\t(a0),d0\n"; out << "\tmove.l\td0,-(a7)\n"; }
				else out << "\tmove." << (isChar ? "b" : "l") << "\td0,(a0)\n";
			}
			else if (op == "LOADG" && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannte globale Variable " + ins.args[0]);
				out << "\tmove.l\ttc_g_" << ins.args[0] << "(pc),-(a7)\n";
			}
			else if (op == "STOREG" && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannte globale Variable " + ins.args[0]);
				// PC-relative Adressierung ist beim 68000 nur Quelle, nicht Ziel.
				// a0 ist hier ein kurzlebiger Adress-Temporaer des Machine Backends.
				out << "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_" << ins.args[0] << "(pc),a0\n\tmove.l\td0,(a0)\n";
			}
			else if (op == "LOADGC" && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannte globale Variable " + ins.args[0]);
				out << "\tmoveq\t#0,d0\n\tmove.b\ttc_g_" << ins.args[0] << "(pc),d0\n\tmove.l\td0,-(a7)\n";
			}
			else if (op == "STOREGC" && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannte globale Variable " + ins.args[0]);
				out << "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_" << ins.args[0] << "(pc),a0\n\tmove.b\td0,(a0)\n";
			}
			else if ((op == "LOADGP" || op == "STOREGP") && ins.args.size() == 1) {
				if (globalByName.find(ins.args[0]) == globalByName.end()) throw std::runtime_error("unbekannte globale Variable");
				if (op == "LOADGP") out << "\tmove.l\ttc_g_" << ins.args[0] << "(pc),-(a7)\n";
				else out << "\tmove.l\t(a7)+,d0\n\tlea\ttc_g_" << ins.args[0] << "(pc),a0\n\tmove.l\td0,(a0)\n";
			}
			else if (op == "PTRINDEX" && ins.args.size() == 1) {
				out << "\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n";
				if (ins.args[0] != "c" && ins.args[0] != "b") out << "\tlsl.l\t#2,d0\n";
				out << "\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n";
			}
			else if ((op == "LOADIND" || op == "STOREIND") && ins.args.size() == 1) {
				bool byte = ins.args[0] == "c" || ins.args[0] == "b";
				if (op == "LOADIND") { out << "\tmove.l\t(a7)+,a0\n"; if (byte) out << "\tmoveq\t#0,d0\n\tmove.b\t(a0),d0\n"; else out << "\tmove.l\t(a0),d0\n"; out << "\tmove.l\td0,-(a7)\n"; }
				else out << "\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n\tmove." << (byte ? "b" : "l") << "\td0,(a0)\n";
			}
			else if ((op == "PADD" || op == "PSUB") && ins.args.size() == 1) {
				out << "\tmove.l\t(a7)+,d0\n\tmove.l\t(a7)+,a0\n";
				if (ins.args[0] != "c" && ins.args[0] != "b") out << "\tlsl.l\t#2,d0\n";
				if (op == "PSUB") out << "\tneg.l\td0\n";
				out << "\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n";
			}
			else if (op == "IPADD" && ins.args.size() == 1) {
				out << "\tmove.l\t(a7)+,a0\n\tmove.l\t(a7)+,d0\n";
				if (ins.args[0] != "c" && ins.args[0] != "b") out << "\tlsl.l\t#2,d0\n";
				out << "\tadda.l\td0,a0\n\tmove.l\ta0,-(a7)\n";
			}
			else if (op == "PDIFF" && ins.args.size() == 1) {
				out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n";
				if (ins.args[0] != "c" && ins.args[0] != "b") out << "\tasr.l\t#2,d0\n";
				out << "\tmove.l\td0,-(a7)\n";
			}
			else if (op == "ADD") out << "\tmove.l\t(a7)+,d1\n\tadd.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n";
			else if (op == "SUB") out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tsub.l\td1,d0\n\tmove.l\td0,-(a7)\n";
			else if (op == "NEG") out << "\tneg.l\t(a7)\n";
			else if (op == "NOT") out << "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tseq\td0\n\tandi.l\t#1,d0\n\tmove.l\td0,-(a7)\n";
			else if (op == "NOTBIT") out << "\tnot.l\t(a7)\n";
			else if (op == "BAND") out << "\tmove.l\t(a7)+,d1\n\tand.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n";
			else if (op == "BXOR") out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\teor.l\td1,d0\n\tmove.l\td0,-(a7)\n";
			else if (op == "BOR") out << "\tmove.l\t(a7)+,d1\n\tor.l\t(a7)+,d1\n\tmove.l\td1,-(a7)\n";
			else if (op == "SHL" || op == "SHR" || op == "USHR") {
				out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\t" << (op == "SHL" ? "lsl" : op == "SHR" ? "asr" : "lsr") << ".l\td1,d0\n\tmove.l\td0,-(a7)\n";
			}
			else if (op == "NARROWC") out << "\tmove.l\t(a7),d0\n\tandi.l\t#255,d0\n\tmove.l\td0,(a7)\n";
			else if (op == "DUP" || op == "DUPP") out << "\tmove.l\t(a7),-(a7)\n";
			else if (op == "MUL" || op == "DIV" || op == "UDIV" || op == "MOD" || op == "UMOD") {
				out << "\tmove.l\t(a7)+,d1\n\tmove.l\t(a7)+,d0\n\tbsr\ttc_" << (op == "MUL" ? "mul_i32" : op == "DIV" ? "div_i32" : op == "UDIV" ? "udiv_u32" : op == "MOD" ? "mod_i32" : "umod_u32") << "\n\tmove.l\td0,-(a7)\n";
			}
			else if (op == "CMPLT") emitCompare(out, "blt", serial);
			else if (op == "CMPGT") emitCompare(out, "bgt", serial);
			else if (op == "CMPLE") emitCompare(out, "ble", serial);
			else if (op == "CMPGE") emitCompare(out, "bge", serial);
			else if (op == "CMPEQ") emitCompare(out, "beq", serial);
			else if (op == "CMPNE") emitCompare(out, "bne", serial);
			else if (op == "CMPULT") emitCompare(out, "bcs", serial);
			else if (op == "CMPUGT") emitCompare(out, "bhi", serial);
			else if (op == "CMPULE") emitCompare(out, "bls", serial);
			else if (op == "CMPUGE") emitCompare(out, "bcc", serial);
			else if (op == "PCMPEQ") emitCompare(out, "beq", serial);
			else if (op == "PCMPNE") emitCompare(out, "bne", serial);
			else if (op == "PCMPLT") emitCompare(out, "bcs", serial);
			else if (op == "PCMPGT") emitCompare(out, "bhi", serial);
			else if (op == "PCMPLE") emitCompare(out, "bls", serial);
			else if (op == "PCMPGE") emitCompare(out, "bcc", serial);
			else if (op == "LABEL" && ins.args.size() == 1) out << "tc_" << ins.args[0] << ":\n";
			else if (op == "JMP" && ins.args.size() == 1) out << "\tbra\ttc_" << ins.args[0] << "\n";
			else if (op == "JZ" && ins.args.size() == 1) out << "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbeq\ttc_" << ins.args[0] << "\n";
			else if (op == "JNZ" && ins.args.size() == 1) out << "\tmove.l\t(a7)+,d0\n\ttst.l\td0\n\tbne\ttc_" << ins.args[0] << "\n";
			else if ((op == "CALL" || op == "CALLP") && ins.args.size() == 2) {
				const int nargs = asInt(ins.args[1], ins.line);
				if (byName.find(ins.args[0]) == byName.end()) throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannte Funktion " + ins.args[0]);
				out << "\tbsr\ttc_" << ins.args[0] << "\n";
				if (nargs) out << "\tlea\t" << (nargs * 4) << "(a7),a7\n";
				out << "\tmove.l\td0,-(a7)\n";
			}
			else if (op == "RET" || op == "RETP") out << "\tmove.l\t(a7)+,d0\n\tunlk\ta6\n\trts\n";
			else if (op == "DROP") out << "\taddq.l\t#4,a7\n";
			else if (op == "PRINT") out << "\tmove.l\t(a7)+,d0\n\tbsr\ttc_putint\n";
			else if (op == "PRINTU") out << "\tmove.l\t(a7)+,d0\n\tbsr\ttc_putuint\n";
			else if (op == "PRINTC") out << "\tmove.l\t(a7)+,d0\n\tbsr\ttc_putchar\n";
			else throw std::runtime_error("IR Zeile " + std::to_string(ins.line) + ": unbekannter oder unvollstaendiger Opcode " + op);
		}
		out << "\n";
	}
	emitM68kCore(out);
	// Target-Runtime-Stubs: austauschbar; kein absoluter Zugriff und damit PIC-freundlich.
	out << "tc_putint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n";
	out << "tc_putuint:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n";
	out << "tc_putchar:\trts\t; Target Runtime ersetzt dies spaeter durch Ausgabe\n";
	out << "tc_exit:\trts\t; Target Runtime beendet den Prozess\n";
	bool hasData = false, hasBss = false;
	for (const Global& global : globals) { hasData |= (!global.isArray && global.initialValue != 0) || !global.init.empty(); hasBss |= (global.isArray && global.init.empty()) || (!global.isArray && global.initialValue == 0); }
	if (hasData) {
		out << "\n\t; DATA-Aequivalent des flachen Einzelmoduls: statisch initialisierte int32-Globals\n\teven\n";
		for (const Global& global : globals) if (global.initialValue != 0 || !global.init.empty()) {
			if (!global.isChar) out << "\teven\n";
			if (global.init.empty()) out << "tc_g_" << global.name << ":\tdc." << (global.isChar ? "b" : "l") << "\t" << global.initialValue << "\n";
			else { out << "tc_g_" << global.name << ":\n"; for (int value : global.init) out << "\tdc." << (global.isChar ? "b" : "l") << "\t" << value << "\n"; }
		}
	}
	if (hasBss) {
		out << "\n\t; BSS-Aequivalent des flachen Einzelmoduls: nullinitialisierte int32-Globals\n\teven\n";
		for (const Global& global : globals) if ((global.isArray && global.init.empty()) || (!global.isArray && global.initialValue == 0)) {
			if (!global.isChar) out << "\teven\n";
			out << "tc_g_" << global.name << ":\t" << (global.isArray ? "ds." : "dc.") << (global.isChar ? "b" : "l") << "\t" << (global.isArray ? global.length : 0) << "\n";
		}
	}
}

int main(int argc, char* argv[]) {
	try {
		if (argc != 3) {
			std::cerr << "usage: " << argv[0] << " <input.ir> <output.s68>\n";
			return 2;
		}
		const std::vector<Instr> ir = readIR(argv[1]);
		const std::vector<Global> globals = findGlobals(ir);
		const std::vector<Function> funcs = findFunctions(ir);
		std::ofstream out(argv[2]);
		if (!out) throw std::runtime_error(std::string("kann Ausgabe nicht schreiben: ") + argv[2]);
		emitIR(out, ir, funcs, globals);
		if (!out) throw std::runtime_error("Schreibfehler in Assembler-Ausgabe");
	}
	catch (const std::exception& e) {
		std::cerr << "qcc_backend: " << e.what() << "\n";
		return 1;
	}
	return 0;
}
