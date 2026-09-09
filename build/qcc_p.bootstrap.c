typedef int FILE;
typedef unsigned int size_t;
/* `stderr` is a Microware-stdio macro (`&_niob[2]`), not a linkable clib
   symbol.  QCC's bootstrap subset has no macro expansion and only needs the
   diagnostic stream on rejected input.  A private null stream keeps the
   successful compiler path independent of that internal stdio object. */
static FILE* stderr;
extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern int fclose(FILE*);
extern int fprintf(FILE*, const char*, ...);
extern int printf(const char*, ...);
extern int fputc(int, FILE*);
extern int fputs(const char*, FILE*);
extern int sprintf(char*, const char*, ...);
extern size_t strlen(const char*);
extern char* strchr(const char*, int);
extern int strncmp(const char*, const char*, size_t);
/* realloc braucht der erzeugte Parser an zwei Stellen: fuer das Aktions-Log
   und fuer den Eingabepuffer in main(). Die Deklaration stand frueher im
   Rumpf des Parsers und ueberlebte hier nur, weil diese Stelle zufaellig
   HINTER dem Marker oben liegt; seit genParserC sie unbedingt im Kopf ausgibt
   (also davor), faellt sie mit dem Header-Vorspann weg. Sie gehoert ohnehin
   hierher -- das ist die Liste aller libc-Funktionen, die die
   Bootstrap-Teilmenge braucht. Signatur wie von genParserC ausgegeben: die
   Teilmenge kennt kein size_t-typisiertes Allozieren, und auf dem 68k-Ziel
   sind int und Zeiger beide 32 Bit. */
extern char* realloc(char*, int);
static const char* p;
static const char* parserInputStart;
static const char* parserActionAt;
static int actionLogLen = 0;	 
static int actionErrors = 0;	 

static int idch(int c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')
	    || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static const char wsSet[] = " \x09\x0d\x0a";
static void ws(void) {
	for (;;) {
		if (*p && strchr(wsSet, *p)) { p++; continue; }
		if (strncmp(p, "//", 2) == 0) { while (*p && *p != '\n') p++; continue; }
		if (strncmp(p, "/*", 2) == 0) { p += 2; while (*p && strncmp(p, "*/", 2) != 0) p++; if (*p) p += 2; continue; }
		return;
	}
}

extern void exit(int);
typedef struct { int id; const char* start; const char* end; } ActionLogEntry;
static ActionLogEntry* actionLog;
static int actionLogCap;
static void actionLogDispatch(int id, const char* start, const char* end);
static void actionLogPush(int id, const char* start, const char* end) {
	if (actionLogLen == actionLogCap) { int n = actionLogCap ? actionLogCap * 2 : 1024; ActionLogEntry* q = (ActionLogEntry*)realloc((char*)actionLog, n * sizeof(ActionLogEntry)); if (!q) { fprintf(stderr, "qcc: kein Speicher fuer Aktions-Log\n"); exit(1); } actionLog = q; actionLogCap = n; }
	actionLog[actionLogLen].id = id;
	actionLog[actionLogLen].start = start;
	actionLog[actionLogLen].end = end;
	actionLogLen++;
}
static void actionLogReplay(void) {
	int i;
	for (i = 0; i < actionLogLen; i++) actionLogDispatch(actionLog[i].id, actionLog[i].start, actionLog[i].end);
}

 
 
typedef struct TCType {
	char base;                     
	unsigned char pointers;        
	unsigned char structId;        
	unsigned char pointeeConst;   





 
} TCType;









 
static char tcStructNames[16][32];
static int  tcStructFieldCount[16];
static char tcStructFieldNames[16][16][32];
static TCType tcStructFieldTypes[16][16];
static int  tcStructFieldOffset[16][16];







 

static int  tcStructFieldArrayLen[16][16];  

 
static int  tcStructFieldRowLen[16][16];














 
static char tcStructFieldConst[16][16];
static int  tcFieldConst = 0;
static int  tcStructByteSize[16];
static int  tcStructCount = 0;
static char tcStructBuildName[32];
static int  tcStructBuildFieldCount = 0;
static char tcStructBuildFieldNames[16][32];
static TCType tcStructBuildFieldTypes[16];
static int  tcStructBuildFieldArrayLen[16];
static int  tcStructBuildFieldRowLen[16];
static char tcStructBuildFieldConst[16];



 
static int  tcAnonStructPending = 0;
static char tcTypedefNames[32][32];
static TCType tcTypedefTypes[32];
static int  tcTypedefCount = 0;





 
static TCType tcFnSigRet[16];
static int    tcFnSigNargs[16];
static TCType tcFnSigParams[16][8];
static int    tcFnSigCount = 0;
static TCType tcFnPtrRetPending;      



 
static char tcEnumConstNames[128][32];
static long tcEnumConstValues[128];
static int  tcEnumConstCount = 0;
static char tcEnumTypeNames[16][32];
static int  tcEnumTypeCount = 0;









 
static char tcNames[256][32];         
static int  tcLocalCount = 0;
static TCType tcLocalTypes[256];
static int  tcLocalArrayLen[256];      
static int  tcLocalArrayNDims[256];    
static int  tcLocalArrayDims[256][6 - 1];  
static int  tcLocalConst[256];         


















 
static int  tcLocalDead[256];          
static int  tcScopeMark[64];     
static int  tcScopeDepth = 0;
static char tcGlobalNames[512][32];    
static int  tcGlobalCount = 0;
static TCType tcGlobalTypes[512];
static int  tcGlobalArrayLen[512];
static int  tcGlobalArrayNDims[512];   
static int  tcGlobalArrayDims[512][6 - 1];  
static int  tcGlobalConst[512];        








 
static int  tcFunctionIsDeclOnly[512];
static int  tcFunctionIsStatic[512];
static int  tcGlobalIsDeclOnly[512];
static int  tcGlobalIsStatic[512];
static int  tcIdxNDScratchDeclared[6 + 1];  
static int  tcPtrIdxScratchDeclared[6 + 1];  
static int  tcStructCopyScratchDeclared;  
static int  tcStructRetDeclared[16];  
static int  tcStructArgSeq;  


 
static int  tcLocalStructByAddr[256];

 
static void tcEmitStructCopy(int dstSlot, const char* dstGlobal, int size);
static int  tcStringCounter = 0;     
static int  tcPendingConst = 0;       
static int  tcPendingStatic = 0;     
 
static int  tcCurrentFuncIndex = -1; 
 
static int  tcFuncNameIsStatic = 0;  
 
static char tcStaticLocalName[32];    
static int  tcStaticRuntimeInitPending = 0;  
static TCType tcCurrentType;
static int  tcBasePointers = 0; 






 
static TCType tcFuncType;
static char tcFuncName[32];
static char tcFunctionNames[512][32];


 
static TCType tcFunctionReturnTypes[512];
static TCType tcFunctionParamTypes[512][64];
static int tcFunctionNargs[512];
static int tcFunctionCount = 0;
static int tcFunctionIsExternal[512];   
static int tcFunctionIsVariadic[512];   

 
static char tcExternName[32];
static TCType tcExternReturnType;
static TCType tcExternBuildParamTypes[64];
static int tcExternBuildParamCount = 0;
static int tcExternIsVariadic = 0;
static char tcCallName[64][32];      
static int  tcCallArgCount[64];
static int  tcCallFnSig[64];         
static char tcCallSavedAdd[64];
static char tcCallSavedMul[64];
static char tcCallSavedRel0[64];
static char tcCallSavedRel1[64];
static int  tcCallDepth = 0;
static char tcPendingAdd = 0;        
static char tcPendingMul = 0;        
static char tcParenSavedAdd[64];
static char tcParenSavedMul[64];
static char tcParenSavedRel0[64];
static char tcParenSavedRel1[64];
static int  tcParenDepth = 0;

 
static int  tcCommaHasValue = 0;









 
static int  tcParenSavedTgtSlot[64];
static char tcParenSavedTgtGlobal[64][32];
static int  tcParenSavedTgtIsGlobal[64];
static TCType tcParenSavedTgtType[64];
static int  tcParenSavedTgtIsArray[64];
static int  tcParenSavedTgtIndirect[64];
static char tcParenSavedAssignOp[64][3];


 
void tc_assign(const char* start, const char* end);
static void tcAssignStore(int leaveValue);
static char tcIndexSavedAdd[64];
static char tcIndexSavedMul[64];
static char tcIndexSavedRel0[64];
static char tcIndexSavedRel1[64];
static int  tcIndexDepth = 0;
static char tcRel0 = 0;  
static char tcRel1 = 0;
static int  tcTargetSlot = -1;
static char tcTargetGlobal[32];
static int  tcTargetIsGlobal = 0;
static TCType tcTargetType;
static int  tcTargetIsArray = 0;
static int  tcTargetIndirect = 0;

 
static int  tcPrevTargetSlot = -1;
static char tcPrevTargetGlobal[32];
static int  tcPrevTargetIsGlobal = 0;
static TCType tcPrevTargetType;
static int  tcPrevTargetIsArray = 0;
static int  tcPrevTargetIndirect = 0;


 
static int  tcChainHandled = 0;
static char tcAssignOp[3];
static TCType tcValueTypes[256];
static int  tcValueDepth = 0;
static TCType tcRetType;
static int  tcArgCount = 0;
static int  tcRetHasVal = 0;
static int  tcLastWasPrint = 0;
static int  tcNextLabel = 0;







 
static char tcGotoNames[512][32];
static int  tcGotoLabel[512];     
static char tcGotoDefined[512];   
static char tcGotoUsed[512];      
static int  tcGotoCount = 0;














 

static int tcCtrlTop[128];
static int tcCtrlEnd[128];
static int tcCtrlCont[128];
static int tcCtrlExtra[128];
static char tcCtrlKind[128];       
static int  tcCtrlDepth = 0;


 
static int tcSwitchBodyLabel[16];
static int tcSwitchNextLabel[16];
static int tcSwitchEndLabel[16];
static int tcSwitchGroupOpen[16];
static int tcSwitchHadDefault[16];
static int  tcSwitchDepth = 0;



 
static TCType tcCastStack[16];
static int  tcCastDepth = 0;
static char tcLogicKind[64];
static int tcLogicBranch[64];
static int tcLogicDone[64];
static int tcLogicDepth = 0;
static char tcBitKind[64];
static int  tcBitDepth = 0;
static char tcPendingShift0 = 0;
static char tcPendingShift1 = 0;
static int tcTernaryFalse[64];
static int tcTernaryDone[64];
static int tcTernaryDepth = 0;






 
static char tcTernSavedAdd[64];
static char tcTernSavedMul[64];
static char tcTernSavedRel0[64];
static char tcTernSavedRel1[64];
static TCType tcTernaryTrueType[64];








 
static void tcErrAt(const char* at) {
	const char* q; const char* lineStart; int line;
	if (at == 0 || parserInputStart == 0) { fprintf(stderr, "qcc: "); return; }
	line = 1; lineStart = parserInputStart;
	for (q = parserInputStart; q < at; q++) {
		if (*q == '\n') { line++; lineStart = q + 1; }
	}
	fprintf(stderr, "qcc: %d:%d: ", line, (int)(at - lineStart) + 1);
}
static void tcPushCtrl(char kind, int top, int cont, int end, int extra) {
	if (tcCtrlDepth >= 128) { actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "control nesting too deep\n"); return; }
	tcCtrlKind[tcCtrlDepth] = kind;
	tcCtrlTop[tcCtrlDepth] = top;
	tcCtrlCont[tcCtrlDepth] = cont;
	tcCtrlEnd[tcCtrlDepth] = end;
	tcCtrlExtra[tcCtrlDepth] = extra;
	tcCtrlDepth++;
}
static int tcNeedCtrl(char kind) {
	if (tcCtrlDepth > 0 && tcCtrlKind[tcCtrlDepth - 1] == kind) return 1;
	actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "internal control-frame mismatch\n");
	return 0;
}

 
static int tcFindLoop(void) {
	int i;
	for (i = tcCtrlDepth - 1; i >= 0; i--) {
		if (tcCtrlKind[i] == 'w' || tcCtrlKind[i] == 'f' || tcCtrlKind[i] == 'd') return i;
	}
	return -1;
}
 
static int tcFindBreakTarget(void) {
	int i;
	for (i = tcCtrlDepth - 1; i >= 0; i--) {
		if (tcCtrlKind[i] == 'w' || tcCtrlKind[i] == 'f' || tcCtrlKind[i] == 'd' || tcCtrlKind[i] == 's') return i;
	}
	return -1;
}
static void tcCopy(char* dst, const char* s, const char* e) {
	int n = 0; const char* q;
	for (q = s; q < e && n < 31; q++) dst[n++] = *q;
	dst[n] = 0;
}
static int tcEq(const char* a, const char* b) {
	while (*a && *b) { if (*a != *b) return 0; a++; b++; }
	return *a == *b;
}





 
static void tcIdentFromSpan(char* dst, const char* s, const char* e, const char* skipKw) {
	const char* p = s;
	int n = 0;
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	if (skipKw) {
		const char* k = skipKw;
		while (p < e && *k && *p == *k) { p++; k++; }
		while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	}
	while (p < e && n < 31 &&
	       ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	        (*p >= '0' && *p <= '9') || *p == '_')) {
		dst[n++] = *p++;
	}
	dst[n] = 0;
}

 
static int tcGotoFind(const char* name) {
	int i;
	for (i = 0; i < tcGotoCount; i++)
		if (tcEq(tcGotoNames[i], name)) return i;
	if (tcGotoCount >= 512) {
		tcErrAt(parserActionAt); fprintf(stderr, "too many goto labels\n"); actionErrors++; return -1;
	}
	tcCopy(tcGotoNames[tcGotoCount], name, name + strlen(name));
	tcGotoLabel[tcGotoCount] = tcNextLabel++;
	tcGotoDefined[tcGotoCount] = 0;
	tcGotoUsed[tcGotoCount] = 0;
	return tcGotoCount++;
}



 
static int tcLookupLocal(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = tcLocalCount - 1; i >= 0; i--) if (!tcLocalDead[i] && tcEq(tcNames[i], name)) return i;
	return -1;
}
static int tcLookupGlobal(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcGlobalCount; i++) if (tcEq(tcGlobalNames[i], name)) return i;
	return -1;
}

 
static const char* tcWordEnd(const char* s, const char* e) {
	const char* p = s;
	while (p < e && (*p == '_' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) p++;
	return p;
}
static int tcEqSpan(const char* s, const char* e, const char* lit) {
	while (s < e && *lit && *s == *lit) { s++; lit++; }
	return s == e && *lit == 0;
}
static int tcLookupStruct(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcStructCount; i++) if (tcEq(tcStructNames[i], name)) return i;
	return -1;
}
static char tcDiagVarRef[128];
static int tcLookupStructField(int sid, const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	if (sid < 0 || sid >= tcStructCount) {
		return -1;
	}
	for (i = 0; i < tcStructFieldCount[sid]; i++) if (tcEq(tcStructFieldNames[sid][i], name)) return i;
	return -1;
}
static int tcLookupTypedef(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcTypedefCount; i++) if (tcEq(tcTypedefNames[i], name)) return i;
	return -1;
}
static int tcLookupEnumConst(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcEnumConstCount; i++) if (tcEq(tcEnumConstNames[i], name)) return i;
	return -1;
}
static int tcLookupEnumType(const char* s, const char* e) {
	char name[32]; int i; tcCopy(name, s, e);
	for (i = 0; i < tcEnumTypeCount; i++) if (tcEq(tcEnumTypeNames[i], name)) return i;
	return -1;
}
static TCType tcMakeType(char base, int pointers) { TCType t; t.base = base; t.pointers = (unsigned char)pointers; t.structId = 0; t.pointeeConst = 0; return t; }


 
static int tcDecodeStringLit(const char* start, const char* end, unsigned char* bytes, int cap);
static TCType tcBadType(void) { return tcMakeType('?', 0); }

 
static int tcSameType(TCType a, TCType b) { return a.base == b.base && a.pointers == b.pointers && ((a.base != 's' && a.base != 'F') || a.structId == b.structId); }
static int tcIsPointer(TCType t) { return t.pointers != 0; }
static int tcIsFnPtr(TCType t) { return t.base == 'F' && t.pointers == 1; }
static TCType tcMakeFnPtr(int sigId) {
	TCType t = tcMakeType('F', 1);
	t.structId = (unsigned char)(sigId + 1);
	return t;
}
static int tcIsInteger(TCType t) { return !t.pointers && (t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'z'); }
static int tcIsBool(TCType t) { return !t.pointers && t.base == 'b'; }










 
static int tcIsTruthy(TCType t) { return t.pointers != 0 || t.base == 'b' || t.base == 'i' || t.base == 'u' || t.base == 'c' || t.base == 'z'; }
static TCType tcPointerTo(TCType t) { if (t.pointers < 255) t.pointers++; else actionErrors++; return t; }
static TCType tcPointee(TCType t) { if (t.pointers) t.pointers--; else actionErrors++; return t; }
static char tcTypeTag(TCType t) { return t.pointers ? 'p' : (t.base == 'c' || t.base == 'b') ? t.base : 'i'; }











 












 
static void tcEmitElemIndexStep(TCType t) {
	if (t.base == 's' && !t.pointers)
		printf("IPADDN %d\n", tcStructByteSize[t.structId - 1]);
	else
		printf("PTRINDEX %c\n", tcTypeTag(t));
}


































 
static TCType tcEmitPtrFieldIndex(int sid, int fi, int laden) {
	TCType el = tcPointee(tcStructFieldTypes[sid][fi]);

	



 
	if (!laden && tcStructFieldTypes[sid][fi].pointeeConst
	    && tcStructFieldTypes[sid][fi].pointers == 1) {
		tcErrAt(parserActionAt);
		fprintf(stderr, "cannot assign through pointer to const\n");
		actionErrors++;
	}
	printf("LOADIND p\n");
	if (el.base == 's' && !el.pointers) {
		printf("IPADDN %d\n", tcStructByteSize[el.structId - 1]);
		return el;
	}
	printf("IPADD %c\n", tcTypeTag(el));
	if (laden)
		printf("LOADIND %c\n", tcTypeTag(el));
	return el;
}

static void tcEmitFieldIndexStep(int sid, int fi) {
	if (tcIsPointer(tcStructFieldTypes[sid][fi])) printf("IPADDN %d\n", 8);
	else printf("IPADD %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
}







 
static void tcStashChainedIndex(void) {
	if (!tcPtrIdxScratchDeclared[2]) { printf("GLOBAL __ptrIdx_2 0 i 1\n"); tcPtrIdxScratchDeclared[2] = 1; }
	printf("STOREG __ptrIdx_2\n");
}
static void tcUnstashChainedIndex(void) {
	printf("LOADG __ptrIdx_2\nSWAP\n");
}




 
static TCType tcEmitFieldRowColIndex(int sid, int fi, int laden) {
	int elemSize = tcTypeTag(tcStructFieldTypes[sid][fi]) == 'c' ? 1 : 4;
	printf("IPADDN %d\n", tcStructFieldRowLen[sid][fi] * elemSize);
	tcUnstashChainedIndex();
	tcEmitFieldIndexStep(sid, fi);
	if (laden) printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
	return tcStructFieldTypes[sid][fi];
}



 
static TCType tcEmitStashedFieldIndex(int sid, int fi, int laden) {
	tcUnstashChainedIndex();
	tcEmitFieldIndexStep(sid, fi);
	if (laden) printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
	return tcStructFieldTypes[sid][fi];
}
static TCType tcPromoteInteger(TCType a, TCType b) { return tcMakeType(a.base == 'u' || b.base == 'u' ? 'u' : 'i', 0); }
static TCType tcLocalType(int slot) { return slot >= 0 && slot < tcLocalCount ? tcLocalTypes[slot] : tcMakeType('i', 0); }
static TCType tcGlobalType(int slot) { return slot >= 0 && slot < tcGlobalCount ? tcGlobalTypes[slot] : tcMakeType('i', 0); }
static int tcLookupFunction(const char* name) { int i; for (i = 0; i < tcFunctionCount; i++) if (tcEq(tcFunctionNames[i], name)) return i; return -1; }

 
static int tcLookupFunction2(const char* s, const char* e) {
	char name[32];
	tcCopy(name, s, e);
	return tcLookupFunction(name);
}



 
static int tcFnSigForFunction(int fnIdx) {
	int i, k;
	if (tcFunctionNargs[fnIdx] > 8) {
		tcErrAt(parserActionAt); fprintf(stderr, "function has too many parameters for a function pointer\n");
		actionErrors++; return -1;
	}
	for (i = 0; i < tcFnSigCount; i++) {
		if (tcFnSigNargs[i] != tcFunctionNargs[fnIdx]) continue;
		if (!tcSameType(tcFnSigRet[i], tcFunctionReturnTypes[fnIdx])) continue;
		for (k = 0; k < tcFnSigNargs[i]; k++)
			if (!tcSameType(tcFnSigParams[i][k], tcFunctionParamTypes[fnIdx][k])) break;
		if (k == tcFnSigNargs[i]) return i;
	}
	if (tcFnSigCount >= 16) {
		tcErrAt(parserActionAt); fprintf(stderr, "too many function pointer signatures\n");
		actionErrors++; return -1;
	}
	tcFnSigRet[tcFnSigCount] = tcFunctionReturnTypes[fnIdx];
	tcFnSigNargs[tcFnSigCount] = tcFunctionNargs[fnIdx];
	for (k = 0; k < tcFunctionNargs[fnIdx]; k++)
		tcFnSigParams[tcFnSigCount][k] = tcFunctionParamTypes[fnIdx][k];
	return tcFnSigCount++;
}
static void tcTypePush(TCType type) { if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = type; else actionErrors++; }
static TCType tcTypePop(void) { return tcValueDepth > 0 ? tcValueTypes[--tcValueDepth] : tcBadType(); }


 
static void tcTypePush4(int base, int pointers, int structId, int pointeeConst) {
	if (tcValueDepth >= 256) { actionErrors++; return; }
	tcValueTypes[tcValueDepth].base = (unsigned char)base;
	tcValueTypes[tcValueDepth].pointers = (unsigned char)pointers;
	tcValueTypes[tcValueDepth].structId = (unsigned char)structId;
	tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)pointeeConst;
	tcValueDepth++;
}
static void tcTypePop4(TCType* out) {
	if (tcValueDepth <= 0) { out->base = '?'; out->pointers = 0; out->structId = 0; out->pointeeConst = 0; actionErrors++; return; }
	tcValueDepth--;
	out->base = tcValueTypes[tcValueDepth].base;
	out->pointers = tcValueTypes[tcValueDepth].pointers;
	out->structId = tcValueTypes[tcValueDepth].structId;
	out->pointeeConst = tcValueTypes[tcValueDepth].pointeeConst;
}
static int tcCompatible4(const TCType* wanted, const TCType* got) {
	if (wanted->base == got->base && wanted->pointers == got->pointers &&
	    ((wanted->base != 's' && wanted->base != 'F') || wanted->structId == got->structId)) return 1;
	if (wanted->pointers && got->pointers && wanted->pointers == got->pointers &&
	    (wanted->base == 'v' || got->base == 'v')) return 1;
	if (wanted->pointers) return !got->pointers && got->base == 'z';
	if ((wanted->base == 'i' || wanted->base == 'u' || wanted->base == 'c' || wanted->base == 'z') &&
	    !wanted->pointers && !got->pointers && got->base == 'b') return 1;
	return !wanted->pointers && !got->pointers &&
	       (wanted->base == 'i' || wanted->base == 'u' || wanted->base == 'c' || wanted->base == 'z') &&
	       (got->base == 'i' || got->base == 'u' || got->base == 'c' || got->base == 'z');
}



 




 
static void tcDerefIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* p = start;
	const char* ns; const char* ne;
	int slot, global = -1; TCType pt, vt; char tag; const char* ld;
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	ns = p; ne = tcWordEnd(ns, end);
	slot = tcLookupLocal(ns, ne);
	if (slot < 0) global = tcLookupGlobal(ns, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - ns), ns); actionErrors++; tcTypePush(tcBadType()); return; }
	pt = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIsPointer(pt)) { tcErrAt(start); fprintf(stderr, "'*' requires a pointer\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	vt = tcPointee(pt);
	if (tcIsPointer(vt) || vt.base == 's' || vt.base == 'b') {
		tcErrAt(start); fprintf(stderr, "++/-- through a pointer is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tag = tcTypeTag(vt);
	ld = slot >= 0 ? "LOADP" : "LOADGP";
	if (!isPre) {
		 
		if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
		printf("LOADIND %c\n", tag);
	}
	if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
	if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
	printf("LOADIND %c\nPUSH 1\n%s\nSTOREIND %c\n", tag, isDec ? "SUB" : "ADD", tag);
	if (isPre) {
		if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
		printf("LOADIND %c\n", tag);
	}
	tcTypePush(vt);
}



 
static void tcIndexIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* ne = tcWordEnd(start, end);
	int slot = tcLookupLocal(start, ne), global = -1;
	TCType et; char tag; char buf[64];
	if (slot < 0) global = tcLookupGlobal(start, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
	et = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (tcIsPointer(et) || et.base == 's' || et.base == 'b') {
		tcErrAt(start); fprintf(stderr, "++/-- on an indexed element is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tag = tcTypeTag(et);
	if (slot >= 0) sprintf(buf, "L %d %c", slot, tag); else sprintf(buf, "G %s %c", tcGlobalNames[global], tag);
	if (isPre) {
		printf("DUP\nDUP\nLOADIDX %s\nPUSH 1\n%s\nSTOREIDX %s\nLOADIDX %s\n",
		       buf, isDec ? "SUB" : "ADD", buf, buf);
	} else {
		printf("DUP\nLOADIDX %s\nSWAP\nDUP\nLOADIDX %s\nPUSH 1\n%s\nSTOREIDX %s\n",
		       buf, buf, isDec ? "SUB" : "ADD", buf);
	}
	tcTypePush(et);
}




 
static void tcMemberIncDec(const char* start, const char* end, int isDec, int isPre) {
	const char* ne = tcWordEnd(start, end);
	const char* fs; const char* fe;
	int slot, global = -1, viaPtr, sid, fi;
	TCType bt, ft; char tag;
	if (ne >= end) { tcErrAt(start); fprintf(stderr, "bad member increment\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	viaPtr = (*ne == '-');
	fs = ne + (viaPtr ? 2 : 1);
	fe = tcWordEnd(fs, end);
	slot = tcLookupLocal(start, ne);
	if (slot < 0) global = tcLookupGlobal(start, ne);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(ne - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
	bt = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (viaPtr) {
		if (!tcIsPointer(bt) || tcPointee(bt).base != 's') { tcErrAt(start); fprintf(stderr, "'->' requires a pointer to struct\n"); actionErrors++; tcTypePush(tcBadType()); return; }
		sid = tcPointee(bt).structId - 1;
	} else {
		if (tcIsPointer(bt) || bt.base != 's') { tcErrAt(start); fprintf(stderr, "'.' requires a struct\n"); actionErrors++; tcTypePush(tcBadType()); return; }
		sid = bt.structId - 1;
	}
	fi = tcLookupStructField(sid, fs, fe);
	if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fe - fs), fs); actionErrors++; tcTypePush(tcBadType()); return; }
	ft = tcStructFieldTypes[sid][fi];
	if (tcIsPointer(ft) || ft.base == 's' || ft.base == 'b' || tcStructFieldArrayLen[sid][fi] > 0) {
		tcErrAt(start); fprintf(stderr, "++/-- on a struct field is only supported for int/unsigned/char in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tag = tcTypeTag(ft);
	printf("PUSH %d\n", tcStructFieldOffset[sid][fi]);
	if (viaPtr) { if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]); }
	else        { if (slot >= 0) {
			
 
			if (tcLocalStructByAddr[slot]) printf("LOADP %d\n", slot);
			else printf("PUSHADDR L %d\n", slot);
		} else printf("PUSHADDR G %s\n", tcGlobalNames[global]); }
	printf("IPADD c\n");
	if (isPre) printf("DUP\nDUP\nLOADIND %c\nPUSH 1\n%s\nSTOREIND %c\nLOADIND %c\n", tag, isDec ? "SUB" : "ADD", tag, tag);
	else       printf("DUP\nLOADIND %c\nSWAP\nDUP\nLOADIND %c\nPUSH 1\n%s\nSTOREIND %c\n", tag, tag, isDec ? "SUB" : "ADD", tag);
	tcTypePush(ft);
}
static void tcIncDecEmit(int slot, int global, TCType t, int isDec, int isPre) {
	char tag = tcTypeTag(t);
	const char* loadOp;
	const char* storeOp;
	const char* ld;
	const char* st;
	char ptag;
	if (tcIsPointer(t)) {
		



 
		ld = slot >= 0 ? "LOADP" : "LOADGP";
		st = slot >= 0 ? "STOREP" : "STOREGP";
		ptag = tcTypeTag(tcPointee(t));
		if (isPre) {
			printf("PUSH %d\n", isDec ? -1 : 1);
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("IPADD %c\nDUP\n", ptag);
		} else {
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("PUSH %d\n", isDec ? -1 : 1);
			if (slot >= 0) printf("%s %d\n", ld, slot); else printf("%s %s\n", ld, tcGlobalNames[global]);
			printf("IPADD %c\n", ptag);
		}
		if (slot >= 0) printf("%s %d\n", st, slot); else printf("%s %s\n", st, tcGlobalNames[global]);
		tcTypePush(t);
		return;
	}
	loadOp = slot >= 0 ? (tag == 'i' ? "LOADL" : "LOADC") : (tag == 'i' ? "LOADG" : "LOADGC");
	storeOp = slot >= 0 ? (tag == 'i' ? "STOREL" : "STOREC") : (tag == 'i' ? "STOREG" : "STOREGC");
	if (slot >= 0) printf("%s %d\n", loadOp, slot); else printf("%s %s\n", loadOp, tcGlobalNames[global]);
	if (isPre) printf("PUSH 1\n%s\nDUP\n", isDec ? "SUB" : "ADD");
	else printf("DUP\nPUSH 1\n%s\n", isDec ? "SUB" : "ADD");
	if (slot >= 0) printf("%s %d\n", storeOp, slot); else printf("%s %s\n", storeOp, tcGlobalNames[global]);
	tcTypePush(t);
}
static int tcIncDecCheck(int slot, int global, TCType t) {
	


 
	if (t.base == 's' || t.base == 'b') return 0;
	if (slot >= 0 && tcLocalArrayLen[slot]) return 0;
	if (slot < 0 && global >= 0 && tcGlobalArrayLen[global]) return 0;
	return 1;
}
static int tcCompatible(TCType wanted, TCType got) {
	if (tcSameType(wanted, got)) return 1;
	

 
	if (tcIsPointer(wanted) && tcIsPointer(got) && wanted.pointers == got.pointers && (wanted.base == 'v' || got.base == 'v')) return 1;
	if (tcIsPointer(wanted)) return !tcIsPointer(got) && got.base == 'z';
	
 
	if (tcIsInteger(wanted) && tcIsBool(got)) return 1;
	return tcIsInteger(wanted) && tcIsInteger(got);
}
static void tcPrintType(FILE* out, TCType t) {
	int i; const char* name;
	


 
	if (t.base == 's' && t.structId > 0 && t.structId <= 16) {
		fputs("struct ", out); fputs(tcStructNames[t.structId - 1], out);
		for (i = 0; i < t.pointers; i++) fputc('*', out);
		return;
	}
	if (t.base == 'F') {
		fputs("function pointer", out);
		return;
	}
	name = t.base == 'u' ? "unsigned int" : t.base == 'c' ? "char" : t.base == 'b' ? "bool" : t.base == 'z' ? "null" : t.base == 'i' ? "int" : t.base == 'v' ? "void" : t.base == 's' ? "struct" : "?";
	fputs(name, out); for (i = 0; i < t.pointers; i++) fputc('*', out);
}
static void tcTypeError(const char* what, TCType wanted, TCType got) {
	tcErrAt(parserActionAt); fprintf(stderr, "%s expects ", what); tcPrintType(stderr, wanted);
	fputs(", got ", stderr); tcPrintType(stderr, got); fputc('\n', stderr); actionErrors++;
}
static void tcLogicBegin(char kind) {
	TCType left; int branch, end;
	tcTypePop4(&left);
	if (!tcIsTruthy(left)) tcTypeError("logical operator", tcMakeType('b', 0), left);
	if (tcLogicDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "logical nesting too deep\n"); actionErrors++; return; }
	branch = tcNextLabel++; end = tcNextLabel++;
	tcLogicKind[tcLogicDepth] = kind; tcLogicBranch[tcLogicDepth] = branch; tcLogicDone[tcLogicDepth] = end; tcLogicDepth++;
	printf("%s L%d\n", kind == '&' ? "JZ" : "JNZ", branch);
}
static int tcHasToken(const char* s, const char* e, char a, char b) {
	const char* p; for (p = s; p + 1 < e; p++) if (p[0] == a && p[1] == b) return 1;
	return 0;
}
static int tcHasChar(const char* s, const char* e, char c) {
	const char* p; for (p = s; p < e; p++) if (*p == c) return 1;
	return 0;
}
static int tcHasTopToken(const char* s, const char* e, char a, char b) {
	const char* p; int round = 0, square = 0; char quote = 0;
	for (p = s; p < e; p++) {
		if (quote) {
			if (*p == '\\' && p + 1 < e) { p++; continue; }
			if (*p == quote) quote = 0;
			continue;
		}
	
 
	if (*p == 39 || *p == 34) { quote = *p; continue; }
		if (*p == '(') round++;
		else if (*p == ')') round--;
		else if (*p == '[') square++;
		else if (*p == ']') square--;
		else if (!round && !square && p + 1 < e && p[0] == a && p[1] == b) return 1;
	}
	return 0;
}
static int tcHasTopChar(const char* s, const char* e, char c) {
	const char* p; int round = 0, square = 0; char quote = 0;
	for (p = s; p < e; p++) {
		if (quote) {
			if (*p == '\\' && p + 1 < e) { p++; continue; }
			if (*p == quote) quote = 0;
			continue;
		}
	if (*p == 39 || *p == 34) { quote = *p; continue; }
		if (*p == '(') round++;
		else if (*p == ')') round--;
		else if (*p == '[') square++;
		else if (*p == ']') square--;
		else if (!round && !square && *p == c) return 1;
	}
	return 0;
}
static void tcLogicEnd(char kind) {
	TCType right; int frame;
	if (tcLogicDepth <= 0 || tcLogicKind[tcLogicDepth - 1] != kind) { tcErrAt(parserActionAt); fprintf(stderr, "logical-frame mismatch\n"); actionErrors++; return; }
	tcTypePop4(&right); if (!tcIsTruthy(right)) tcTypeError("logical operator", tcMakeType('b', 0), right);
	frame = --tcLogicDepth;
	printf("%s L%d\nPUSH %d\nJMP L%d\nLABEL L%d\nPUSH %d\nLABEL L%d\n",
		kind == '&' ? "JZ" : "JNZ", tcLogicBranch[frame], kind == '&' ? 1 : 0,
		tcLogicDone[frame], tcLogicBranch[frame], kind == '&' ? 0 : 1, tcLogicDone[frame]);
	tcTypePush4('b', 0, 0, 0);
}
static void tcBitBegin(char kind) {
	if (tcBitDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "bitwise nesting too deep\n"); actionErrors++; return; }
	tcBitKind[tcBitDepth++] = kind;
}
static void tcBitEnd(char kind) {
	TCType right, left;
	if (tcBitDepth <= 0 || tcBitKind[tcBitDepth - 1] != kind) { tcErrAt(parserActionAt); fprintf(stderr, "bitwise-frame mismatch\n"); actionErrors++; return; }
	tcBitDepth--;
	right = tcTypePop(); left = tcTypePop();
	if (!tcIsInteger(left)) tcTypeError("bitwise operator", tcMakeType('i', 0), left);
	if (!tcIsInteger(right)) tcTypeError("bitwise operator", tcMakeType('i', 0), right);
	tcTypePush(tcPromoteInteger(left, right));
	printf("%s\n", kind == '&' ? "BAND" : kind == '^' ? "BXOR" : "BOR");
}
static void tcShiftEnd(void) {
	TCType right = tcTypePop(), left = tcTypePop();
	if (!tcIsInteger(left)) tcTypeError("shift", tcMakeType('i', 0), left);
	if (!tcIsInteger(right)) tcTypeError("shift", tcMakeType('i', 0), right);
	tcTypePush(left.base == 'u' ? left : tcMakeType('i', 0));
	printf("%s\n", tcPendingShift0 == '<' ? "SHL" : left.base == 'u' ? "USHR" : "SHR");
	tcPendingShift0 = tcPendingShift1 = 0;
}
static void tcTernaryBegin(void) {
	TCType condition = tcTypePop(); int falseLabel, endLabel;
	if (!tcIsTruthy(condition)) tcTypeError("conditional condition", tcMakeType('b', 0), condition);
	if (tcTernaryDepth >= 64) { tcErrAt(parserActionAt); fprintf(stderr, "conditional nesting too deep\n"); actionErrors++; return; }
	falseLabel = tcNextLabel++; endLabel = tcNextLabel++;
	tcTernaryFalse[tcTernaryDepth] = falseLabel;
	tcTernaryDone[tcTernaryDepth] = endLabel;
	tcTernSavedAdd[tcTernaryDepth] = tcPendingAdd;
	tcTernSavedMul[tcTernaryDepth] = tcPendingMul;
	tcTernSavedRel0[tcTernaryDepth] = tcRel0;
	tcTernSavedRel1[tcTernaryDepth] = tcRel1;
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcTernaryDepth++;
	printf("JZ L%d\n", falseLabel);
}
static void tcTernaryMiddle(void) {
	if (tcTernaryDepth <= 0) { tcErrAt(parserActionAt); fprintf(stderr, "conditional-frame mismatch\n"); actionErrors++; return; }
	tcTernaryTrueType[tcTernaryDepth - 1] = tcTypePop();
	printf("JMP L%d\nLABEL L%d\n", tcTernaryDone[tcTernaryDepth - 1], tcTernaryFalse[tcTernaryDepth - 1]);
}
static void tcTernaryEnd(void) {
	TCType falseType, trueType, result;
	if (tcTernaryDepth <= 0) { tcErrAt(parserActionAt); fprintf(stderr, "conditional-frame mismatch\n"); actionErrors++; return; }
	falseType = tcTypePop();
	trueType = tcTernaryTrueType[tcTernaryDepth - 1];
	if (tcSameType(trueType, falseType)) result = trueType;
	else if (tcIsPointer(trueType) && falseType.base == 'z' && !falseType.pointers) result = trueType;
	else if (tcIsPointer(falseType) && trueType.base == 'z' && !trueType.pointers) result = falseType;
	else if (tcIsInteger(trueType) && tcIsInteger(falseType)) result = tcPromoteInteger(trueType, falseType);
	else { tcTypeError("conditional branches", trueType, falseType); result = trueType; }
	tcTypePush(result);
	printf("LABEL L%d\n", tcTernaryDone[tcTernaryDepth - 1]);
	tcTernaryDepth--;
	tcPendingAdd = tcTernSavedAdd[tcTernaryDepth];
	tcPendingMul = tcTernSavedMul[tcTernaryDepth];
	tcRel0 = tcTernSavedRel0[tcTernaryDepth];
	tcRel1 = tcTernSavedRel1[tcTernaryDepth];
}
static void tcLoadTarget(void) {
	char tag = tcTypeTag(tcTargetType);
	if (tcTargetIndirect) printf("DUPP\nLOADIND %c\n", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("DUP\nLOADIDX G %s %c\n", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("LOADG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("DUP\nLOADIDX L %d %c\n", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("LOAD%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
	else { tcErrAt(parserActionAt); fprintf(stderr, "unknown assignment target\n"); actionErrors++; return; }
	tcTypePush(tcTargetType);
}
static void tcCompoundAssign(void) {
	TCType right = tcTypePop(), left = tcTypePop();
	if (tcIsPointer(left)) {
		if ((tcAssignOp[0] != '+' && tcAssignOp[0] != '-') || !tcIsInteger(right)) tcTypeError("pointer compound assignment", tcMakeType('i', 0), right);
		else printf("P%s %c\n", tcAssignOp[0] == '+' ? "ADD" : "SUB", tcTypeTag(tcPointee(left)));
		tcTypePush(left); return;
	}
	if (!tcIsInteger(left) || !tcIsInteger(right)) tcTypeError("compound assignment", tcMakeType('i', 0), !tcIsInteger(left) ? left : right);
	if (tcAssignOp[0] == '+') printf("ADD\n");
	else if (tcAssignOp[0] == '-') printf("SUB\n");
	else if (tcAssignOp[0] == '*') printf("MUL\n");
	else if (tcAssignOp[0] == '/') printf("%s", left.base == 'u' || right.base == 'u' ? "UDIV\n" : "DIV\n");
	else if (tcAssignOp[0] == '%') printf("%s", left.base == 'u' || right.base == 'u' ? "UMOD\n" : "MOD\n");
	else if (tcAssignOp[0] == '&') printf("BAND\n");
	else if (tcAssignOp[0] == '^') printf("BXOR\n");
	else if (tcAssignOp[0] == '|') printf("BOR\n");
	else if (tcAssignOp[0] == '<') printf("SHL\n");
	else if (tcAssignOp[0] == '>') printf("%s", left.base == 'u' ? "USHR\n" : "SHR\n");
	tcTypePush(tcTargetType);
}



 
static int tcConstArrayLen(const char** pp, const char* end) {
	const char* p = *pp;
	int v = 0, digits = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); digits = 1; }
	if (!digits) { *pp = p; return 0; }
	for (;;) {
		const char* q = p;
		int rhs = 0, rdig = 0; char op;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		if (q >= end || (*q != '+' && *q != '-' && *q != '*')) break;
		op = *q++;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		while (q < end && *q >= '0' && *q <= '9') { rhs = rhs * 10 + (*q++ - '0'); rdig = 1; }
		if (!rdig) break;
		if (op == '+') v += rhs; else if (op == '-') v -= rhs; else v *= rhs;
		p = q;
	}
	*pp = p;
	return v;
}
static long tcNum(const char* s, const char* e) {
	long v = 0; const char* q;
	 
	if (e - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (q = s + 2; q < e; q++) {
			int d;
			if (*q >= '0' && *q <= '9') d = *q - '0';
			else if (*q >= 'a' && *q <= 'f') d = *q - 'a' + 10;
			else if (*q >= 'A' && *q <= 'F') d = *q - 'A' + 10;
			else break;
			v = v * 16 + d;
		}
		return v;
	}
	for (q = s; q < e; q++) v = v * 10 + (*q - '0');
	return v;
}
static const char* tcNameEnd(const char* s, const char* e) {
	const char* p = s;
	
 
	while (p < e && *p != '[' && *p != '.' && *p != '-') p++;
	return p;
}
static int tcConstIndex(const char* s, const char* e, int* value) {
	const char* p = s;
	int v = 0, digits = 0;
	while (p < e && *p != '[') p++;
	if (p == e) return 0;
	p++;
	while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); digits = 1; }
	if (!digits || p >= e || *p != ']') return 0;
	p++;
	return p == e ? (*value = v, 1) : 0;
}
static void tcCheckConstIndex(const char* text, const char* end, int length) {
	int index;
	if (tcConstIndex(text, end, &index) && (index < 0 || index >= length)) {
		tcErrAt(parserActionAt); fprintf(stderr, "constant array index %d out of range (length %d)\n", index, length);
		actionErrors++;
	}
}

 
static int tcCountTopIndexes(const char* p, const char* end) {
	int depth = 0, count = 0;
	while (p < end) {
		if (*p == '[') { if (depth == 0) count++; depth++; }
		else if (*p == ']') { if (depth > 0) depth--; }
		p++;
	}
	return count;
}




 
static const char* tcSkipOneIndex(const char* p, const char* end) {
	int depth = 0;
	while (p < end) {
		if (*p == '[') depth++;
		else if (*p == ']') { depth--; p++; if (depth == 0) return p; continue; }
		p++;
	}
	return p;
}


 
static const char* tcSkipAllIndexes(const char* p, const char* end) {
	while (p < end && *p == '[') p = tcSkipOneIndex(p, end);
	return p;
}











 
static void tcEmitNDCombine(int ndims, const int* trailingDims) {
	char name[24]; int lvl;
	for (lvl = ndims; lvl >= 2; lvl--) {
		sprintf(name, "__idxNd_%d", lvl);
		if (!tcIdxNDScratchDeclared[lvl]) { printf("GLOBAL %s 0 i 1\n", name); tcIdxNDScratchDeclared[lvl] = 1; }
		printf("STOREG %s\n", name);
	}
	for (lvl = 2; lvl <= ndims; lvl++) {
		sprintf(name, "__idxNd_%d", lvl);
		printf("PUSH %d\nMUL\nLOADG %s\nADD\n", trailingDims[lvl - 2], name);
	}
}








 



 
static int tcCheckNDIndex(int ndims, const int* trailingDims, int idxCount) {
	char name[24]; int lvl;
	if (idxCount == ndims) { if (ndims > 1) tcEmitNDCombine(ndims, trailingDims); return 1; }
	if (idxCount > 0 && idxCount < ndims) {
		 
		for (lvl = idxCount; lvl >= 2; lvl--) {
			sprintf(name, "__idxNd_%d", lvl);
			if (!tcIdxNDScratchDeclared[lvl]) { printf("GLOBAL %s 0 i 1\n", name); tcIdxNDScratchDeclared[lvl] = 1; }
			printf("STOREG %s\n", name);
		}
		for (lvl = 2; lvl <= idxCount; lvl++) {
			sprintf(name, "__idxNd_%d", lvl);
			printf("PUSH %d\nMUL\nLOADG %s\nADD\n", trailingDims[lvl - 2], name);
		}
		for (lvl = idxCount; lvl < ndims; lvl++) printf("PUSH %d\nMUL\n", trailingDims[lvl - 1]);
		return 2;
	}
	if (ndims == 1 && idxCount == 2) { tcErrAt(parserActionAt); fprintf(stderr, "array is not two-dimensional\n"); actionErrors++; return 0; }
	tcErrAt(parserActionAt); fprintf(stderr, "array has %d dimension(s), but %d index(es) were given\n", ndims, idxCount);
	actionErrors++; return 0;
}



 
static TCType tcEmitPointerIndexChain(int slot, const char* globalName, char base, int pointers, int structId, int pointeeConst, int idxCount) {
	char name[24]; int level; TCType pointer = tcMakeType(base, pointers);
	pointer.structId = (unsigned char)structId;
	pointer.pointeeConst = (unsigned char)pointeeConst;
	for (level = idxCount; level >= 2; level--) {
		sprintf(name, "__ptrIdx_%d", level);
		if (!tcPtrIdxScratchDeclared[level]) { printf("GLOBAL %s 0 i 1\n", name); tcPtrIdxScratchDeclared[level] = 1; }
		printf("STOREG %s\n", name);
	}
	if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", globalName);
	for (level = 1; level <= idxCount; level++) {
		TCType value = tcPointee(pointer);
		if (!tcIsPointer(pointer)) { tcErrAt(parserActionAt); fprintf(stderr, "too many pointer indexes\n"); actionErrors++; return tcBadType(); }
		if (!tcIsPointer(value) && value.base == 'v') { tcErrAt(parserActionAt); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; return tcMakeType('i', 0); }
		if (level > 1) {
			sprintf(name, "__ptrIdx_%d", level);
			printf("LOADG %s\nSWAP\n", name);
		}
		if (value.base == 's' && !value.pointers) {
			










 
			printf("IPADDN %d\n", tcStructByteSize[value.structId - 1]);
		} else {
			printf("PTRINDEX %c\nLOADIND %c\n", tcTypeTag(value), tcTypeTag(value));
		}
		pointer = value;
	}
	return pointer;
}
static int tcInitList(const char* start, const char* end, long* values, int cap) {
	const char* p = start; int count = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p == end || *p != '{') return -1;
	p++;
	for (;;) {
		int neg = 0; long value = 0; int digits = 0;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p < end && *p == '}') return count;
		if (p < end && *p == '-') { neg = 1; p++; }
		while (p < end && *p >= '0' && *p <= '9') { value = value * 10 + (*p++ - '0'); digits = 1; }
		if (!digits || count >= cap) return -2;
		values[count++] = neg ? -value : value;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (p < end && *p == ',') { p++; continue; }
		if (p < end && *p == '}') return count;
		return -2;
	}
}
void tc_defname(const char* start, const char* end) {
	int i;
	tcLocalCount = 0;
	
 
	for (i = 0; i < 256; i++) tcLocalDead[i] = 0;
	tcScopeDepth = 0;
	tcGotoCount = 0;           
	tcFuncType.base = tcCurrentType.base;
	tcFuncType.pointers = tcCurrentType.pointers;
	tcFuncType.structId = tcCurrentType.structId;
	tcFuncType.pointeeConst = tcCurrentType.pointeeConst;
	tcCopy(tcFuncName, start, end);
	



 
	tcFuncNameIsStatic = tcPendingStatic;
	tcPendingStatic = 0;
}

void tc_const(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingConst = 1;
}

void tc_fieldconst(const char* start, const char* end) {
	(void)start; (void)end;
	tcFieldConst = 1;
}

void tc_fieldconstend(const char* start, const char* end) {
	
 
	(void)start; (void)end;
	tcFieldConst = 0;
}

void tc_static(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingStatic = 1;
}


 
void tc_externname(const char* start, const char* end) {
	tcCopy(tcExternName, start, end);
	tcExternReturnType = tcCurrentType;
	tcExternBuildParamCount = 0;
	tcExternIsVariadic = 0;
}

void tc_externparam(const char* start, const char* end) {
	(void)start; (void)end;
	tcPendingConst = 0;  
	if (tcExternBuildParamCount < 64) tcExternBuildParamTypes[tcExternBuildParamCount++] = tcCurrentType;
	else { tcErrAt(start); fprintf(stderr, "too many extern parameters\n"); actionErrors++; }
}

void tc_externvariadic(const char* start, const char* end) {
	(void)start; (void)end;
	tcExternIsVariadic = 1;
}








 
void tc_externdeclend(const char* start, const char* end) {
	int i; (void)start; (void)end;
	if (tcLookupFunction(tcExternName) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate function '%s'\n", tcExternName); actionErrors++; return; }
	if (tcFunctionCount >= 512) { tcErrAt(start); fprintf(stderr, "too many functions\n"); actionErrors++; return; }
	i = tcFunctionCount++;
	tcCopy(tcFunctionNames[i], tcExternName, tcExternName + strlen(tcExternName));
	tcFunctionReturnTypes[i] = tcExternReturnType;
	tcFunctionNargs[i] = tcExternBuildParamCount;
	{ int p; for (p = 0; p < tcExternBuildParamCount; p++) tcFunctionParamTypes[i][p] = tcExternBuildParamTypes[p]; }
	tcFunctionIsExternal[i] = 1;
	tcFunctionIsVariadic[i] = tcExternIsVariadic;
}

static void tc_setcurrenttype(int base, int pointers) {
	
 
	tcCurrentType.base = (unsigned char)base;
	tcCurrentType.pointers = (unsigned char)pointers;
	tcCurrentType.structId = 0;
	tcCurrentType.pointeeConst = 0;
}

void tc_type(const char* start, const char* end) {
	const char* we = tcWordEnd(start, end);
	

 
	tcBasePointers = 0;
	if (tcEqSpan(start, we, "unsigned")) {
		


 
		const char* q = we; const char* qe;
		while (q < end && (*q == ' ' || *q == '\t')) q++;
		qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) do { tcCurrentType.base = (unsigned char)('c'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
		else do { tcCurrentType.base = (unsigned char)('u'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	}
	else if (tcEqSpan(start, we, "int")) do { tcCurrentType.base = (unsigned char)('i'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	 
	else if (tcEqSpan(start, we, "long")) do { tcCurrentType.base = (unsigned char)('i'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	else if (tcEqSpan(start, we, "char")) do { tcCurrentType.base = (unsigned char)('c'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	else if (tcEqSpan(start, we, "bool")) do { tcCurrentType.base = (unsigned char)('b'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	else if (tcEqSpan(start, we, "void")) do { tcCurrentType.base = (unsigned char)('v'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	else if (tcEqSpan(start, we, "struct")) {
		const char* p = we; const char* ne; int sid;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { tcErrAt(start); fprintf(stderr, "unknown struct '%.*s'\n", (int)(ne - p), p); actionErrors++; do { tcCurrentType.base = (unsigned char)('?'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0); return; }
		do { tcCurrentType.base = (unsigned char)('s'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0); tcCurrentType.structId = (unsigned char)(sid + 1);
	} else if (tcEqSpan(start, we, "enum")) {
		const char* p = we; const char* ne;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		if (tcLookupEnumType(p, ne) < 0) { tcErrAt(start); fprintf(stderr, "unknown enum '%.*s'\n", (int)(ne - p), p); actionErrors++; do { tcCurrentType.base = (unsigned char)('?'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0); return; }
		do { tcCurrentType.base = (unsigned char)('i'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0);
	} else {
		int td = tcLookupTypedef(start, we);
		if (td < 0) { tcErrAt(start); fprintf(stderr, "unknown type name '%.*s'\n", (int)(we - start), start); actionErrors++; do { tcCurrentType.base = (unsigned char)('?'); tcCurrentType.pointers = (unsigned char)(0); tcCurrentType.structId = 0; tcCurrentType.pointeeConst = 0; } while (0); return; }
		tcCurrentType.base = tcTypedefTypes[td].base;
		tcCurrentType.pointers = tcTypedefTypes[td].pointers;
		tcCurrentType.structId = tcTypedefTypes[td].structId;
		tcCurrentType.pointeeConst = tcTypedefTypes[td].pointeeConst;
		tcBasePointers = tcCurrentType.pointers;
	}
}









 
void tc_pointerdecl(const char* start, const char* end) {
	const char* p;
	int n = tcBasePointers;
	
 
	for (p = start; p < end; p++) if (*p == '*') n++;
	if (n > 255) { n = 255; actionErrors++; }
	tcCurrentType.pointers = (unsigned char)n;
}

void tc_param(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end);
	if (tcLocalCount >= 256) { tcErrAt(start); fprintf(stderr, "too many locals\n"); actionErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, nameEnd);
	 
	tcLocalTypes[tcLocalCount].base = tcCurrentType.base;
	tcLocalTypes[tcLocalCount].pointers = tcCurrentType.pointers + (nameEnd < end ? 1 : 0);
	tcLocalTypes[tcLocalCount].structId = tcCurrentType.structId;
	tcLocalTypes[tcLocalCount].pointeeConst = tcCurrentType.pointeeConst;
	if (!tcLocalTypes[tcLocalCount].pointers && tcLocalTypes[tcLocalCount].base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid parameter type\n"); actionErrors++;
		tcLocalTypes[tcLocalCount].base = 'i';
		tcLocalTypes[tcLocalCount].pointers = 0;
		tcLocalTypes[tcLocalCount].structId = 0;
		tcLocalTypes[tcLocalCount].pointeeConst = 0;
	}
	tcLocalArrayLen[tcLocalCount] = 0;
	tcLocalArrayNDims[tcLocalCount] = 1;
	


 
	tcLocalConst[tcLocalCount] = tcPendingConst && !tcLocalTypes[tcLocalCount].pointers;
	if (tcPendingConst && tcLocalTypes[tcLocalCount].pointers) tcLocalTypes[tcLocalCount].pointeeConst = 1;
	tcLocalDead[tcLocalCount] = 0;
	
 
	tcLocalStructByAddr[tcLocalCount] =
		tcLocalTypes[tcLocalCount].base == 's' && !tcLocalTypes[tcLocalCount].pointers;
	tcPendingConst = 0;
	tcLocalCount++;
}

void tc_funcbegin(const char* start, const char* end) {
	int i, f; (void)start; (void)end;
	tcCurrentFuncIndex = -1;
	f = tcLookupFunction(tcFuncName);
	if (f >= 0) {
		
 
		if (!tcFunctionIsDeclOnly[f] || tcFunctionIsExternal[f] ||
			tcFunctionNargs[f] != tcLocalCount ||
			!tcSameType(tcFunctionReturnTypes[f], tcFuncType)) {
			actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate function '%s'\n", tcFuncName); return;
		}
		for (i = 0; i < tcLocalCount; i++) {
			if (!tcSameType(tcFunctionParamTypes[f][i], tcLocalTypes[i])) {
				tcErrAt(start); fprintf(stderr, "function prototype mismatch\n");
				actionErrors++; return;
			}
		}
		tcCurrentFuncIndex = f;
		return;
	}
	if (tcFunctionCount >= 512) { tcErrAt(start); fprintf(stderr, "too many functions\n"); actionErrors++; return; }
	f = tcFunctionCount++;
	tcCopy(tcFunctionNames[f], tcFuncName, tcFuncName + strlen(tcFuncName));
	tcFunctionReturnTypes[f] = tcFuncType;
	tcFunctionNargs[f] = tcLocalCount;
	for (i = 0; i < tcLocalCount; i++) tcFunctionParamTypes[f][i] = tcLocalTypes[i];
	tcFunctionIsExternal[f] = 0;
	tcFunctionIsVariadic[f] = 0;
	tcFunctionIsStatic[f] = tcFuncNameIsStatic;
	tcFunctionIsDeclOnly[f] = 0;  
	tcCurrentFuncIndex = f;
	

 
}

 
void tc_funcbodybegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicDepth = 0;
	tcBitDepth = 0;
	tcPendingShift0 = 0;
	tcPendingShift1 = 0;
	tcIndexDepth = 0;
	if (tcCurrentFuncIndex >= 0) tcFunctionIsDeclOnly[tcCurrentFuncIndex] = 0;
	printf("FUNC %s %d %d\n", tcFuncName, tcLocalCount, tcCurrentFuncIndex >= 0 ? tcFunctionIsStatic[tcCurrentFuncIndex] : 0);
}






 
void tc_funcdeclend(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcCurrentFuncIndex < 0) return;
	











 
	tcFunctionIsDeclOnly[tcCurrentFuncIndex] = 1;
	printf("FUNCDECL %s %d\n", tcFuncName, tcFunctionNargs[tcCurrentFuncIndex]);
}

void tc_funcend(const char* start, const char* end) {
	int gi;
	(void)start; (void)end;
	


 
	for (gi = 0; gi < tcGotoCount; gi++) {
		if (tcGotoUsed[gi] && !tcGotoDefined[gi]) {
			tcErrAt(start); fprintf(stderr, "undefined label '%s'\n", tcGotoNames[gi]);
			actionErrors++;
		}
	}
	if (tcCurrentFuncIndex >= 0 && tcFunctionIsDeclOnly[tcCurrentFuncIndex]) return;
	 
	printf("PUSH 0\n%s\nENDFUNC\n", tcIsPointer(tcFuncType) ? "RETP" : "RET");
}

void tc_local(const char* start, const char* end) {
	if (tcLocalCount >= 256) { tcErrAt(start); fprintf(stderr, "too many locals\n"); actionErrors++; return; }
	tcCopy(tcNames[tcLocalCount], start, end);
	tcLocalTypes[tcLocalCount] = tcCurrentType;
	if (!tcIsPointer(tcLocalTypes[tcLocalCount]) && tcLocalTypes[tcLocalCount].base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++;
		tcLocalTypes[tcLocalCount] = tcCurrentType = tcMakeType('i', 0);
	}
	



 
	tcLocalArrayLen[tcLocalCount] = 0;
	tcLocalArrayNDims[tcLocalCount] = 1;
	
 
	tcLocalConst[tcLocalCount] = tcPendingConst && !tcIsPointer(tcLocalTypes[tcLocalCount]);
	if (tcPendingConst && tcIsPointer(tcLocalTypes[tcLocalCount])) tcLocalTypes[tcLocalCount].pointeeConst = 1;
	tcLocalDead[tcLocalCount] = 0;
	

 
	tcLocalStructByAddr[tcLocalCount] = 0;
	tcPendingConst = 0;
	tcLocalCount++;
}







 
void tc_localdecl(const char* start, const char* end) {
	const char* p = start; int slot = tcLocalCount - 1;
	int dims[6]; int ndims = 0; int total; int k;
	int isStruct = tcLocalTypes[slot].base == 's';
	int structSize = isStruct ? tcStructByteSize[tcLocalTypes[slot].structId - 1] : 0;
	while (p < end && *p != '[') p++;
	if (p == end) {
		if (isStruct) printf("LARRAY %d c %d\n", slot, structSize);
		return;
	}
	while (p < end && *p == '[') {
		int len;
		p++;
		len = tcConstArrayLen(&p, end);
		if (len <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "array size must be positive\n"); return; }
		if (p >= end || *p != ']') { actionErrors++; tcErrAt(start); fprintf(stderr, "bad array declaration\n"); return; }
		p++;
		if (ndims >= 6) { actionErrors++; tcErrAt(start); fprintf(stderr, "too many array dimensions (max %d)\n", 6); return; }
		dims[ndims++] = len;
	}
	total = dims[0];
	for (k = 1; k < ndims; k++) { total *= dims[k]; tcLocalArrayDims[slot][k - 1] = dims[k]; }
	tcLocalArrayNDims[slot] = ndims;
	tcLocalArrayLen[slot] = total;
	if (isStruct) printf("LARRAY %d c %d\n", slot, total * structSize);
	else printf("LARRAY %d %c %d\n", slot, tcTypeTag(tcLocalTypes[slot]), total);
}

void tc_staticlocalname(const char* start, const char* end) {
	tcCopy(tcStaticLocalName, start, end);
}







 
void tc_staticruntimeinit(const char* start, const char* end) {
	TCType exprType = tcTypePop(); (void)start; (void)end;
	if (!tcCompatible(tcCurrentType, exprType)) tcTypeError("static initializer", tcCurrentType, exprType);
	tcStaticRuntimeInitPending = 1;
}
























 
void tc_staticlocal(const char* start, const char* end) {
	int isConst = tcPendingConst; int i, neg = 0; long value = 0; const char* p = start;
	int runtimeInit = tcStaticRuntimeInitPending;
	tcStaticRuntimeInitPending = 0;
	tcPendingConst = 0;
	tcPendingStatic = 0;
	if (tcCurrentType.base == 's') {
		tcErrAt(start); fprintf(stderr, "static struct locals not yet supported\n");
		actionErrors++;
		return;
	}
	if (!tcIsPointer(tcCurrentType) && tcCurrentType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++;
		return;
	}
	if (!runtimeInit) {
		while (p < end && *p != '=') p++;
		if (p < end) {
			p++;
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'b' && p + 4 <= end && p[0] == 't') value = 1;
			else {
				if (p < end && *p == '-') { neg = 1; p++; }
				while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
				if (neg) value = -value;
			}
			if (tcCurrentType.pointers && value != 0) {
				tcErrAt(start); fprintf(stderr, "static local pointer initializer must be 0\n");
				actionErrors++;
			}
			if (!tcCurrentType.pointers && tcCurrentType.base == 'c') value &= 255;
			if (!tcCurrentType.pointers && tcCurrentType.base == 'b') value = value ? 1 : 0;
		}
	}
	if (tcGlobalCount >= 512) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) {
		if (tcEq(tcGlobalNames[i], tcStaticLocalName)) { actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", tcStaticLocalName); return; }
	}
	tcCopy(tcGlobalNames[tcGlobalCount], tcStaticLocalName, tcStaticLocalName + strlen(tcStaticLocalName));
	tcGlobalTypes[tcGlobalCount] = tcCurrentType;
	tcGlobalConst[tcGlobalCount] = isConst && !tcIsPointer(tcCurrentType);
	if (isConst && tcIsPointer(tcCurrentType)) tcGlobalTypes[tcGlobalCount].pointeeConst = 1;
	

 
	tcGlobalIsStatic[tcGlobalCount] = 1;
	tcGlobalIsDeclOnly[tcGlobalCount] = 0;
	tcGlobalArrayLen[tcGlobalCount++] = 0;
	printf("GLOBAL %s %ld %c 1\n", tcStaticLocalName, value, tcTypeTag(tcCurrentType));
	if (runtimeInit) {
		char flagName[40]; int doInitLabel = tcNextLabel++, afterLabel = tcNextLabel++;
		const char* storeSuffix = tcIsPointer(tcCurrentType) ? "P" : (tcTypeTag(tcCurrentType) == 'i' ? "" : "C");
		sprintf(flagName, "__static_init_%s", tcStaticLocalName);
		printf("GLOBAL %s 0 b 1\n", flagName);
		printf("LOADGC %s\nJZ L%d\n", flagName, doInitLabel);
		printf("DROP\nJMP L%d\n", afterLabel);
		printf("LABEL L%d\n", doInitLabel);
		printf("STOREG%s %s\n", storeSuffix, tcStaticLocalName);
		printf("PUSH 1\nSTOREGC %s\n", flagName);
		printf("LABEL L%d\n", afterLabel);
	}
}







 
static void tc_globalend(const char* start, const char* end);
static void tcGlobalOne(const char* start, const char* end);
void tc_globalend(const char* start, const char* end) {
	const char* p = start;
	const char* declStart;
	const char* seg;
	char buf[1024];
	int depth = 0, n, i;
	
 
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	declStart = p;
	{
		const char* q = p; const char* lastWord = p;
		while (q < end && *q != '[' && *q != '=' && *q != ',' && *q != ';') {
			if (*q == ' ' || *q == '\t') {
				const char* next = q;
				while (next < end && (*next == ' ' || *next == '\t')) next++;
				

 
				if (next >= end || *next == '[' || *next == '=' || *next == ',' || *next == ';') break;
				q = next; lastWord = q;
			} else if (*q == '*') { q++; lastWord = q; }
			else q++;
		}
		declStart = lastWord;
	}
	 
	depth = 0; seg = 0;
	for (p = declStart; p < end; p++) {
		if (*p == '[' || *p == '{') depth++;
		else if (*p == ']' || *p == '}') depth--;
		else if (*p == ',' && depth == 0) { seg = p; break; }
	}
	if (!seg) { tcGlobalOne(start, end); return; }
	 
	n = (int)(seg - start);
	if (n > 1000) { tcErrAt(start); fprintf(stderr, "declaration too long\n"); actionErrors++; return; }
	for (i = 0; i < n; i++) buf[i] = start[i];
	buf[n] = ';'; buf[n + 1] = 0;
	tcGlobalOne(buf, buf + n + 1);
	 
	p = seg + 1;
	for (;;) {
		const char* stop = 0;
		int pre = (int)(declStart - start);
		depth = 0;
		for (seg = p; seg < end; seg++) {
			if (*seg == '[' || *seg == '{') depth++;
			else if (*seg == ']' || *seg == '}') depth--;
			else if ((*seg == ',' || *seg == ';') && depth == 0) { stop = seg; break; }
		}
		if (!stop) stop = end;
		n = 0;
		










 
		for (i = 0; i < pre; i++) if (start[i] != '*') buf[n++] = start[i];
		




 
		buf[n++] = ' ';
		while (p < stop && (*p == ' ' || *p == '\t')) p++;
		while (p < stop) buf[n++] = *p++;
		buf[n++] = ';'; buf[n] = 0;
		tcGlobalOne(buf, buf + n);
		if (stop >= end || *stop == ';') break;
		p = stop + 1;
	}
}
static void tcGlobalOne(const char* start, const char* end) {
const char* p = start; char name[32]; TCType type = tcMakeType('i', 0); int n = 0, neg = 0, arrayLen = 0, arrayNDims = 1, initCount = -1; int hadBrackets = 0; long value = 0, initValues[256]; int i; int arrayDims[6 - 1];
	int isConst = tcPendingConst; int isStatic = 0; tcPendingConst = 0;
	tcPendingStatic = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	


 
	if (end - p >= 6 && p[0] == 's' && p[1] == 't' && p[2] == 'a' && p[3] == 't' && p[4] == 'i' && p[5] == 'c') {
		isStatic = 1;
		p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (end - p >= 5 && p[0] == 'c' && p[1] == 'o' && p[2] == 'n' && p[3] == 's' && p[4] == 't') {
		p += 5;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	


 
	if (end - p >= 6 && p[0] == 's' && p[1] == 't' && p[2] == 'r' && p[3] == 'u' && p[4] == 'c' && p[5] == 't') {
		const char* ne; int sid; p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		ne = tcWordEnd(p, end);
		sid = tcLookupStruct(p, ne);
		if (sid < 0) { tcErrAt(start); fprintf(stderr, "unknown struct '%.*s'\n", (int)(ne - p), p); actionErrors++; return; }
		type = tcMakeType('s', 0); type.structId = (unsigned char)(sid + 1);
		p = ne;
	}
	

 
	else if (end - p >= 8 && p[0] == 'u' && p[1] == 'n' && p[2] == 's' && p[3] == 'i'
	         && p[4] == 'g' && p[5] == 'n' && p[6] == 'e' && p[7] == 'd') {
		const char* q; const char* qe;
		p += 8;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		q = p; qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) { type.base = 'c'; p = qe; }
		else if (tcEqSpan(q, qe, "int") || tcEqSpan(q, qe, "long")) { type.base = 'u'; p = qe; }
		else type.base = 'u';                  
	}
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	 
	else if (end - p >= 4 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g') p += 4;
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	else {
		



 
		const char* ne = tcWordEnd(p, end);
		int td = tcLookupTypedef(p, ne);
		if (td < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad global declaration\n"); return; }
		type = tcTypedefTypes[td];
		p = ne;
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != '=' && *p != ';' && *p != '[' && n < 31) name[n++] = *p++;
	name[n] = 0;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (p < end && *p == '[') {
		int dims[6]; int ndims = 0; int k;
		while (p < end && *p == '[') {
			int len;
			p++;
			len = tcConstArrayLen(&p, end);
			if (p >= end || *p != ']') { actionErrors++; tcErrAt(start); fprintf(stderr, "bad array declaration\n"); return; }
			p++;
			
 
			if (len < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "array size must be positive\n"); return; }
			if (ndims >= 6) { actionErrors++; tcErrAt(start); fprintf(stderr, "too many array dimensions (max %d)\n", 6); return; }
			dims[ndims++] = len;
		}
		hadBrackets = 1;    
		arrayLen = dims[0];
		for (k = 1; k < ndims; k++) { arrayLen *= dims[k]; arrayDims[k - 1] = dims[k]; }
		arrayNDims = ndims;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (p < end && *p == '=') {
		if (!type.pointers && type.base == 's') {
			tcErrAt(start); fprintf(stderr, "struct global cannot have an initializer in this version\n");
			actionErrors++; return;
		}
		p++;
		
 
		if (arrayLen || hadBrackets) {
			






 
			const char* q = p; while (q < end && (*q == ' ' || *q == '\t')) q++;
			if (q < end && *q == '"') {
				const char* strEnd = q + 1; unsigned char strBytes[256]; int strLen;
				while (strEnd < end && *strEnd != '"') { if (*strEnd == '\\' && strEnd + 1 < end) strEnd++; strEnd++; }
				if (strEnd < end) strEnd++;
				if (!type.pointers && type.base == 'c') {
					strLen = tcDecodeStringLit(q, strEnd, strBytes, 256);
					
 
					if (arrayLen == 0) arrayLen = strLen + 1;
					if (strLen > arrayLen) { tcErrAt(start); fprintf(stderr, "string literal too long for array\n"); actionErrors++; return; }
					initCount = strLen < arrayLen ? strLen + 1 : strLen;
					for (i = 0; i < strLen; i++) initValues[i] = strBytes[i];
					if (strLen < arrayLen) initValues[strLen] = 0;
				} else {
					tcErrAt(start); fprintf(stderr, "string literal initializer requires a char array\n"); actionErrors++; return;
				}
			} else {
				initCount = tcInitList(p, end, initValues, 256);
				if (initCount < 0 || initCount > arrayLen) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad or oversized array initializer\n"); return; }
			}
		} else {
			while (p < end && (*p == ' ' || *p == '\t')) p++;
			if (p < end && *p == '{') { actionErrors++; tcErrAt(start); fprintf(stderr, "scalar cannot use array initializer\n"); return; }
			if (!type.pointers && type.base == 'b' && p + 4 <= end && p[0] == 't') value = 1;
			else { if (p < end && *p == '-') { neg = 1; p++; }
				while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
				if (neg) value = -value; }
			if (type.pointers && value != 0) { tcErrAt(start); fprintf(stderr, "global pointer initializer must be 0\n"); actionErrors++; return; }
		}
	}
	if (tcGlobalCount >= 512) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
	for (i = 0; i < tcGlobalCount; i++) if (tcEq(tcGlobalNames[i], name)) { actionErrors++; tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", name); return; }
	tcCopy(tcGlobalNames[tcGlobalCount], name, name + n);
	
 
	if (isConst && type.pointers) type.pointeeConst = 1;
	tcGlobalTypes[tcGlobalCount] = type;
	tcGlobalConst[tcGlobalCount] = isConst && !type.pointers;
	tcGlobalIsStatic[tcGlobalCount] = isStatic;
	tcGlobalIsDeclOnly[tcGlobalCount] = 0;
	tcGlobalArrayNDims[tcGlobalCount] = arrayNDims;
	for (i = 0; i < arrayNDims - 1; i++) tcGlobalArrayDims[tcGlobalCount][i] = arrayDims[i];
	tcGlobalArrayLen[tcGlobalCount++] = arrayLen;
	





 
	if (!type.pointers && type.base == 's') {
		int structSize = tcStructByteSize[type.structId - 1];
		int total = (arrayLen > 0 ? arrayLen : 1) * structSize;
		printf("GARRAY %s c %d %d\n", name, total, isStatic);
		return;
	}
	if (arrayLen) {
		printf("GARRAY %s %c %d %d\n", name, tcTypeTag(type), arrayLen, isStatic);
		for (i = 0; i < initCount; i++) printf("GINIT %s %d %ld\n", name, i, !type.pointers && type.base == 'c' ? (initValues[i] & 255) : initValues[i]);
		return;
	}
	if (!type.pointers && type.base == 'c') value &= 255;
	if (!type.pointers && type.base == 'b') value = value ? 1 : 0;
	printf("GLOBAL %s %ld %c %d\n", name, value, tcTypeTag(type), isStatic);
}







 
void tc_externglobaldecl(const char* start, const char* end) {
	const char* p = start; const char* ne; char name[32]; TCType type = tcMakeType('i', 0); int n = 0, td;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (end - p >= 6 && p[0] == 'e' && p[1] == 'x' && p[2] == 't' && p[3] == 'e' && p[4] == 'r' && p[5] == 'n') {
		p += 6;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
	}
	if (end - p >= 8 && strncmp(p, "unsigned", 8) == 0) {
		const char* q; const char* qe;
		p += 8; while (p < end && (*p == ' ' || *p == '\t')) p++;
		q = p; qe = tcWordEnd(q, end);
		if (tcEqSpan(q, qe, "char")) { type.base = 'c'; p = qe; }
		else if (tcEqSpan(q, qe, "int") || tcEqSpan(q, qe, "long")) { type.base = 'u'; p = qe; }
		else type.base = 'u';
	}
	else if (end - p >= 3 && p[0] == 'i' && p[1] == 'n' && p[2] == 't') p += 3;
	else if (end - p >= 4 && p[0] == 'l' && p[1] == 'o' && p[2] == 'n' && p[3] == 'g') p += 4;
	else if (end - p >= 4 && p[0] == 'c' && p[1] == 'h' && p[2] == 'a' && p[3] == 'r') { p += 4; type.base = 'c'; }
	else if (end - p >= 4 && p[0] == 'b' && p[1] == 'o' && p[2] == 'o' && p[3] == 'l') { p += 4; type.base = 'b'; }
	else if (end - p >= 4 && p[0] == 'v' && p[1] == 'o' && p[2] == 'i' && p[3] == 'd') { p += 4; type.base = 'v'; }
	else {
		ne = tcWordEnd(p, end); td = tcLookupTypedef(p, ne);
		if (td < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad extern global declaration\n"); return; }
		type = tcTypedefTypes[td]; p = ne;
	}
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	while (p < end && *p == '*') { type = tcPointerTo(type); p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
	if (!tcIsPointer(type) && type.base == 'v') {
		tcErrAt(start); fprintf(stderr, "void is not a valid variable type\n"); actionErrors++; return;
	}
	while (p < end && *p != ' ' && *p != '\t' && *p != ';' && n < 31) name[n++] = *p++;
	name[n] = 0;
	if (tcLookupGlobal(name, name + n) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate global '%s'\n", name); actionErrors++; return; }
	if (tcGlobalCount >= 512) { tcErrAt(start); fprintf(stderr, "too many globals\n"); actionErrors++; return; }
	tcCopy(tcGlobalNames[tcGlobalCount], name, name + n);
	tcGlobalTypes[tcGlobalCount] = type;
	tcGlobalConst[tcGlobalCount] = 0;
	tcGlobalArrayNDims[tcGlobalCount] = 1;
	tcGlobalIsStatic[tcGlobalCount] = 0;
	tcGlobalIsDeclOnly[tcGlobalCount] = 1;
	tcGlobalArrayLen[tcGlobalCount++] = 0;
	printf("GLOBALDECL %s %c\n", name, tcTypeTag(type));
}

void tc_varinit(const char* start, const char* end) {
	int slot = tcLocalCount - 1, count, i; long values[256];
	if (slot >= 0 && tcLocalArrayLen[slot]) {
		


 
		{ const char* q = start; while (q < end && (*q == ' ' || *q == '\t')) q++; if (q < end && *q == '"') return; }
		count = tcInitList(start, end, values, 256);
		if (count < 0 || count > tcLocalArrayLen[slot]) { actionErrors++; tcErrAt(start); fprintf(stderr, "bad or oversized array initializer\n"); return; }
		for (i = 0; i < count; i++) printf("PUSH %d\nPUSH %ld\nSTOREIDX L %d %c\n", i, !tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 'c' ? (values[i] & 255) : values[i], slot, tcTypeTag(tcLocalTypes[slot]));
		return;
	}
	if (tcInitList(start, end, values, 256) >= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "scalar cannot use array initializer\n"); return; }
	{ TCType got = tcTypePop(), wanted = tcLocalType(slot); if (!tcCompatible(wanted, got)) tcTypeError("initializer", wanted, got); }
	{
		

 
		TCType initType = tcLocalType(slot);
		if (initType.base == 's' && !initType.pointers) {
			int sid = initType.structId - 1;
			if (sid < 0 || sid >= tcStructCount) {
				tcErrAt(start); fprintf(stderr, "initializer for an unknown struct type\n");
				actionErrors++;
			} else if (slot < 0) {
				tcErrAt(start); fprintf(stderr, "struct initializer without a slot\n");
				actionErrors++;
			} else {
				tcEmitStructCopy(slot, 0, tcStructByteSize[sid]);
			}
			return;
		}
	}
	printf("STORE%s %d\n", tcIsPointer(tcLocalType(slot)) ? "P" : tcTypeTag(tcLocalType(slot)) == 'i' ? "L" : "C", slot);
}

void tc_number(const char* start, const char* end) {
	if (end - start == 4 && start[0] == 't') { printf("PUSH 1\n"); tcTypePush4('b', 0, 0, 0); }
	else if (end - start == 5 && start[0] == 'f') { printf("PUSH 0\n"); tcTypePush4('b', 0, 0, 0); }
	else { long value = tcNum(start, end); printf("PUSH %ld\n", value); tcTypePush4(value == 0 ? 'z' : 'i', 0, 0, 0); }
}





 
static int tcDecodeStringLit(const char* start, const char* end, unsigned char* bytes, int cap) {
	int len = 0;
	const char* p = start + 1; const char* stop = end - 1;
	while (p < stop) {
		unsigned char c;
		if (*p == '\\' && p + 1 < stop) {
			char esc = p[1]; p += 2;
			


 
			if (esc == 'x' || esc == 'X') {
				int v = 0, n = 0;
				while (p < stop && n < 2) {
					int d;
					if (*p >= '0' && *p <= '9') d = *p - '0';
					else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
					else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
					else break;
					v = v * 16 + d; p++; n++;
				}
				c = (unsigned char)v;
			} else if (esc >= '0' && esc <= '7') {
				int v = esc - '0', n = 1;
				while (p < stop && n < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (*p++ - '0'); n++; }
				c = (unsigned char)v;
			}
			else c = esc == 'n' ? 10 : esc == 't' ? 9 : esc == 'r' ? 13 : (unsigned char)esc;
		} else {
			c = (unsigned char)*p++;
		}
		if (len >= cap) { tcErrAt(start); fprintf(stderr, "string literal too long (max %d bytes)\n", cap); actionErrors++; break; }
		bytes[len++] = c;
	}
	return len;
}



 
void tc_string(const char* start, const char* end) {
	unsigned char bytes[256]; int len, i, id;
	len = tcDecodeStringLit(start, end, bytes, 256);
	id = tcStringCounter++;
	printf("GARRAY __str%d c %d 1\n", id, len + 1);
	for (i = 0; i < len; i++) printf("GINIT __str%d %d %d\n", id, i, bytes[i]);
	printf("GINIT __str%d %d 0\n", id, len);
	printf("ADDRG __str%d\n", id);
	tcTypePush(tcMakeType('c', 1));
}














 
void tc_arrayinitstring(const char* start, const char* end) {
	int slot = tcLocalCount - 1;
	unsigned char bytes[256]; int len, i;
	if (slot < 0 || !tcLocalArrayLen[slot]) return;
	if (tcIsPointer(tcLocalTypes[slot]) || tcLocalTypes[slot].base != 'c') {
		tcErrAt(start); fprintf(stderr, "string literal initializer requires a char array\n"); actionErrors++; return;
	}
	len = tcDecodeStringLit(start, end, bytes, 256);
	if (len > tcLocalArrayLen[slot]) { tcErrAt(start); fprintf(stderr, "string literal too long for array\n"); actionErrors++; return; }
	(void)tcTypePop();
	printf("DROP\n");
	for (i = 0; i < len; i++) printf("PUSH %d\nPUSH %d\nSTOREIDX L %d c\n", i, bytes[i], slot);
	if (len < tcLocalArrayLen[slot]) printf("PUSH %d\nPUSH 0\nSTOREIDX L %d c\n", len, slot);
}

void tc_neg(const char* start, const char* end) {
	TCType operand = tcTypePop();
	(void)end;
	if (*start == '!') {
		if (!tcIsTruthy(operand)) tcTypeError("logical negation", tcMakeType('b', 0), operand);
		tcTypePush(tcMakeType('b', 0)); printf("NOT\n");
	} else if (*start == '~') {
		if (!tcIsInteger(operand)) tcTypeError("bitwise negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NOTBIT\n");
	} else {
		if (!tcIsInteger(operand)) tcTypeError("negation", tcMakeType('i', 0), operand);
		tcTypePush(tcMakeType(operand.base == 'u' ? 'u' : 'i', 0)); printf("NEG\n");
	}
}

void tc_varref(const char* start, const char* end) {
	const char* nameEnd = tcNameEnd(start, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(start, nameEnd), global;
	TCType globalType, globalPointee;
	tcCopy(tcDiagVarRef, start, end);
	










 
	global = slot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
	

 
	globalType = global >= 0 ? tcGlobalTypes[global] : tcMakeType('i', 0);
	globalPointee = globalType;
	if (globalPointee.pointers) globalPointee.pointers--; else globalPointee = tcMakeType('i', 0);
	if (indexed && *nameEnd == '[' &&
	    !(slot >= 0 && tcLocalArrayLen[slot] > 0 && tcLocalTypes[slot].base == 's') &&
	    !(slot >= 0 && tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 's') &&
	    !(global >= 0 && tcGlobalArrayLen[global] > 0 && globalType.base == 's') &&
	    !(global >= 0 && globalType.pointers && globalPointee.base == 's')) {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			tcErrAt(start); fprintf(stderr, "indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
	}
	




 
	if (slot >= 0 && indexed && *nameEnd == '[' && tcLocalTypes[slot].pointers && tcLocalTypes[slot].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcLocalTypes[slot].structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (chain) tcStashChainedIndex();
			printf("LOADP %d\nIPADDN %d\nPUSH %d\nPADD c\n", slot, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	



 
	if (slot >= 0 && indexed && *nameEnd == '[' && tcLocalArrayLen[slot] > 0 && tcLocalTypes[slot].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcLocalTypes[slot].structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[slot], tcLocalArrayDims[slot], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[slot]);
			if (chain) tcStashChainedIndex();
			



 
			printf("PUSHADDR L %d\nIPADDN %d\nPUSH %d\nPADD c\n", slot, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	


 
	if (indexed && *nameEnd == '-' && nameEnd + 1 < end && nameEnd[1] == '>') {
		TCType pt;
		const char* fieldStart = nameEnd + 2; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		



 
		if (slot >= 0) pt = tcLocalTypes[slot];
		else if (global >= 0) pt = tcGlobalTypes[global];
		else goto tcArrowSkip;
		if (!pt.pointers || pt.base != 's') goto tcArrowSkip;
		sid = pt.structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		{
			int chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			printf("PUSH %d\n", tcStructFieldOffset[sid][fi]);
			if (slot >= 0) printf("LOADP %d\n", slot); else printf("LOADGP %s\n", tcGlobalNames[global]);
			printf("IPADD c\n");
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (fieldEnd < end && *fieldEnd == '[') {
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				

 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				


 
				int elemSize = tcTypeTag(tcStructFieldTypes[sid][fi]) == 'c' ? 1 : 4;
				tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi] / tcStructFieldRowLen[sid][fi]);
				printf("IPADDN %d\n", tcStructFieldRowLen[sid][fi] * elemSize);
				tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				
 
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	tcArrowSkip: ;
	if (slot >= 0 && indexed && *nameEnd == '.' && tcLocalTypes[slot].base == 's') {
		int sid = tcLocalTypes[slot].structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		 
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			if (tcLocalStructByAddr[slot])
				printf("PUSH %d\nLOADP %d\nIPADD c\n", tcStructFieldOffset[sid][fi], slot);
			else
				printf("PUSH %d\nPUSHADDR L %d\nIPADD c\n", tcStructFieldOffset[sid][fi], slot);
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (hasIndex) {
			



 
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				

 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++;
				tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				
 
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	




 
	if (global >= 0 && indexed && *nameEnd == '[' && globalType.pointers && globalPointee.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = globalPointee.structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (chain) tcStashChainedIndex();
			printf("LOADGP %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '[' && tcGlobalArrayLen[global] > 0 && tcGlobalTypes[global].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			if (chain) tcStashChainedIndex();
			printf("PUSHADDR G %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTypePush(tcEmitStashedFieldIndex(sid, fi, 1)); return; }
			if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
	}
	if (global >= 0 && indexed && *nameEnd == '.' && tcGlobalTypes[global].base == 's') {
		char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, nameEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			printf("PUSH %d\nPUSHADDR G %s\nIPADD c\n", tcStructFieldOffset[sid][fi], gname);
			if (chain) { tcTypePush(tcEmitFieldRowColIndex(sid, fi, 1)); return; }
		}
		if (hasIndex) {
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				

 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
					tcTypePush(tcEmitPtrFieldIndex(sid, fi, 1)); return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++;
				tcTypePush(tcBadType()); return;
			}
			if (tcStructFieldRowLen[sid][fi] > 0) {
				
 
				tcErrAt(start); fprintf(stderr, "indexing a two-dimensional struct field is only supported via '->' in this version\n");
				actionErrors++; tcTypePush(tcBadType()); return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
			tcTypePush(tcStructFieldTypes[sid][fi]); return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(tcStructFieldTypes[sid][fi])); return; }
		printf("LOADIND %c\n", tcTypeTag(tcStructFieldTypes[sid][fi]));
		tcTypePush(tcStructFieldTypes[sid][fi]); return;
	}
	if (slot >= 0) {
		TCType localValueType = tcLocalTypes[slot];
		if (tcLocalArrayLen[slot]) {
			if (!indexed) {
				localValueType.pointers++; printf("PUSHADDR L %d\n", slot); tcTypePush(localValueType); return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[slot], tcLocalArrayDims[slot], tcCountTopIndexes(nameEnd, end)) == 2) {
				printf("PUSHADDR L %d\nIPADD %c\n", slot, tcTypeTag(localValueType));
				localValueType.pointers++; tcTypePush(localValueType); return;
			}
			tcCheckConstIndex(start, end, tcLocalArrayLen[slot]);
			if (localValueType.base == 's' && !localValueType.pointers) {
				

 
				printf("PUSHADDR L %d\nIPADDN %d\n", slot,
				       tcStructByteSize[localValueType.structId - 1]);
				do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(localValueType.base); tcValueTypes[tcValueDepth].pointers = (unsigned char)(localValueType.pointers); tcValueTypes[tcValueDepth].structId = (unsigned char)(localValueType.structId); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(localValueType.pointeeConst); tcValueDepth++; } else actionErrors++; } while (0);
			} else {
			printf("LOADIDX L %d %c\n", slot, tcTypeTag(localValueType)); tcTypePush(localValueType);
			}
		} else if (indexed && tcIsPointer(localValueType)) {
			TCType valueType = tcEmitPointerIndexChain(slot, 0, localValueType.base, localValueType.pointers, localValueType.structId, localValueType.pointeeConst, tcCountTopIndexes(nameEnd, end));
			tcTypePush(valueType);
		} else if (indexed) { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; }
		else if (tcLocalTypes[slot].base == 's' && !tcLocalTypes[slot].pointers) {
			


 
			if (tcLocalStructByAddr[slot]) printf("LOADP %d\n", slot);
			else printf("PUSHADDR L %d\n", slot);
			do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(tcLocalTypes[slot].base); tcValueTypes[tcValueDepth].pointers = (unsigned char)(tcLocalTypes[slot].pointers); tcValueTypes[tcValueDepth].structId = (unsigned char)(tcLocalTypes[slot].structId); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(tcLocalTypes[slot].pointeeConst); tcValueDepth++; } else actionErrors++; } while (0);
		}
		else {
			printf("LOAD%s %d\n", tcLocalTypes[slot].pointers ? "P" : tcLocalTypes[slot].base == 'i' || tcLocalTypes[slot].base == 'u' ? "L" : "C", slot);
			do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(tcLocalTypes[slot].base); tcValueTypes[tcValueDepth].pointers = (unsigned char)(tcLocalTypes[slot].pointers); tcValueTypes[tcValueDepth].structId = (unsigned char)(tcLocalTypes[slot].structId); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(tcLocalTypes[slot].pointeeConst); tcValueDepth++; } else actionErrors++; } while (0);
		}
	} else if ((global = tcLookupGlobal(start, nameEnd)) >= 0) {
		char name[32]; TCType globalValueType = tcGlobalTypes[global]; tcCopy(name, start, nameEnd);
		if (tcGlobalArrayLen[global]) {
			if (!indexed) {
				globalValueType.pointers++; printf("PUSHADDR G %s\n", name); tcTypePush(globalValueType); return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end)) == 2) {
				printf("PUSHADDR G %s\nIPADD %c\n", name, tcTypeTag(globalValueType));
				globalValueType.pointers++; tcTypePush(globalValueType); return;
			}
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
			if (globalValueType.base == 's' && !globalValueType.pointers) {
				 
				printf("PUSHADDR G %s\nIPADDN %d\n", name,
				       tcStructByteSize[globalValueType.structId - 1]);
				do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(globalValueType.base); tcValueTypes[tcValueDepth].pointers = (unsigned char)(globalValueType.pointers); tcValueTypes[tcValueDepth].structId = (unsigned char)(globalValueType.structId); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(globalValueType.pointeeConst); tcValueDepth++; } else actionErrors++; } while (0);
			} else {
			printf("LOADIDX G %s %c\n", name, tcTypeTag(globalValueType)); tcTypePush(globalValueType);
			}
		} else if (indexed && tcIsPointer(globalValueType)) {
			
 
			TCType valueType = globalValueType;
			char valueTag;
			valueType.pointers = valueType.pointers - 1;
			valueTag = valueType.pointers ? 'p' : (valueType.base == 'c' || valueType.base == 'b') ? valueType.base : 'i';
			printf("LOADGP %s\nPTRINDEX %c\nLOADIND %c\n", name, valueTag, valueTag);
			if (tcValueDepth < 256) tcValueTypes[tcValueDepth++] = valueType; else actionErrors++;
		} else if (indexed) { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; }
		else if (globalValueType.base == 's' && !globalValueType.pointers) {
			
 
			printf("ADDRG %s\n", name);
			do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)(globalValueType.base); tcValueTypes[tcValueDepth].pointers = (unsigned char)(globalValueType.pointers); tcValueTypes[tcValueDepth].structId = (unsigned char)(globalValueType.structId); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(globalValueType.pointeeConst); tcValueDepth++; } else actionErrors++; } while (0);
		}
		else { printf("LOADG%s %s\n", tcIsPointer(globalValueType) ? "P" : tcTypeTag(globalValueType) == 'i' ? "" : "C", name); tcTypePush(globalValueType); }
	}
	else {
		int ec = tcLookupEnumConst(start, nameEnd);
		int fnv;
		if (ec >= 0 && !indexed) { printf("PUSH %ld\n", tcEnumConstValues[ec]); tcTypePush(tcMakeType('i', 0)); }
		






 
		else if (!indexed && (fnv = tcLookupFunction2(start, nameEnd)) >= 0) {
			int sig = tcFnSigForFunction(fnv);
			printf("PUSHFN %s\n", tcFunctionNames[fnv]);
			tcTypePush(sig >= 0 ? tcMakeFnPtr(sig) : tcBadType());
		}
		else { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(nameEnd - start), start); actionErrors++; }
	}
}

void tc_addressref(const char* start, const char* end) {
	const char* name = start + 1; const char* nameEnd = tcNameEnd(name, end); int indexed = nameEnd < end;
	int slot = tcLookupLocal(name, nameEnd), global = tcLookupGlobal(name, nameEnd); TCType valueType;
	if (slot >= 0) {
		valueType = tcLocalType(slot);
		if (indexed) {
			if (tcLocalArrayLen[slot]) { tcCheckConstIndex(name, end, tcLocalArrayLen[slot]); printf("PUSHADDR L %d\n", slot); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADP %d\n", slot); }
			else { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; return; }
			tcEmitElemIndexStep(valueType); tcTypePush(tcPointerTo(valueType)); return;
		}
		


 
		if (tcLocalArrayLen[slot] || (valueType.base == 's' && !valueType.pointers))
			printf("PUSHADDR L %d\n", slot);
		else printf("ADDRL %d\n", slot);
		tcTypePush(tcPointerTo(valueType)); return;
	}
	if (global >= 0) {
		char globalName[32]; valueType = tcGlobalType(global); tcCopy(globalName, name, nameEnd);
		if (indexed) {
			if (tcGlobalArrayLen[global]) { tcCheckConstIndex(name, end, tcGlobalArrayLen[global]); printf("PUSHADDR G %s\n", globalName); }
			else if (tcIsPointer(valueType)) { valueType = tcPointee(valueType); printf("LOADGP %s\n", globalName); }
			else { tcErrAt(start); fprintf(stderr, "scalar variable cannot be indexed\n"); actionErrors++; return; }
			tcEmitElemIndexStep(valueType); tcTypePush(tcPointerTo(valueType)); return;
		}
		printf("ADDRG %s\n", globalName); tcTypePush(tcPointerTo(valueType)); return;
	}
	tcErrAt(start); fprintf(stderr, "unknown variable in address expression: '%.*s'\n", (int)(nameEnd - name), name); actionErrors++;
}

void tc_derefref(const char* start, const char* end) {
	TCType pointer = tcTypePop(), valueType; (void)start; (void)end;
	if (!tcIsPointer(pointer)) { tcTypeError("dereference", tcPointerTo(tcMakeType('i', 0)), pointer); tcTypePush(tcMakeType('i', 0)); return; }
	valueType = tcPointee(pointer);
	if (!tcIsPointer(valueType) && valueType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	printf("LOADIND %c\n", tcTypeTag(valueType)); tcTypePush(valueType);
}

void tc_postfixindex(const char* start, const char* end) {
	






 
	TCType pointer = tcTypePop(), valueType; (void)start; (void)end;
	if (!tcIsPointer(pointer)) { tcTypeError("index", tcPointerTo(tcMakeType('i', 0)), pointer); tcTypePush(tcMakeType('i', 0)); return; }
	valueType = tcPointee(pointer);
	if (!tcIsPointer(valueType) && valueType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "cannot index void*\n"); actionErrors++;
		tcTypePush(tcMakeType('i', 0)); return;
	}
	printf("PADD %c\nLOADIND %c\n", tcTypeTag(valueType), tcTypeTag(valueType)); tcTypePush(valueType);
}

void tc_callmember(const char* start, const char* end) {
	


 
	const char* fieldStart; const char* fieldEnd;
	int viaPtr, sid, fi, hasIndex;
	TCType bt, ft;
	if (start >= end) { tcErrAt(start); fprintf(stderr, "bad call member\n"); actionErrors++; tcTypePush(tcBadType()); return; }
	viaPtr = (*start == '-');
	fieldStart = start + (viaPtr ? 2 : 1);
	fieldEnd = tcWordEnd(fieldStart, end);
	bt = tcTypePop();
	if (viaPtr) {
		if (!tcIsPointer(bt) || tcPointee(bt).base != 's') {
			tcErrAt(start); fprintf(stderr, "'->' requires a function returning pointer to struct\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		sid = tcPointee(bt).structId - 1;
	} else {
		if (tcIsPointer(bt) || bt.base != 's') {
			tcErrAt(start); fprintf(stderr, "'.' requires a function returning struct\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		sid = bt.structId - 1;
	}
	fi = tcLookupStructField(sid, fieldStart, fieldEnd);
	if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; tcTypePush(tcBadType()); return; }
	ft = tcStructFieldTypes[sid][fi];
	hasIndex = fieldEnd < end && *fieldEnd == '[';
	if (hasIndex && !tcStructFieldArrayLen[sid][fi]) {
		tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	if (hasIndex) {
		if (tcStructFieldRowLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "two-dimensional field access after a function call is not supported yet\n");
			actionErrors++; tcTypePush(tcBadType()); return;
		}
		tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
		

 
		printf("SWAP\nPUSH %d\nPADD c\nSWAP\n", tcStructFieldOffset[sid][fi]);
		printf("PADD %c\nLOADIND %c\n", tcTypeTag(ft), tcTypeTag(ft));
		tcTypePush(ft); return;
	}
	printf("PUSH %d\nPADD c\n", tcStructFieldOffset[sid][fi]);
	if (tcStructFieldArrayLen[sid][fi] > 0) { tcTypePush(tcPointerTo(ft)); return; }
	printf("LOADIND %c\n", tcTypeTag(ft));
	tcTypePush(ft);
}

void tc_addop(const char* start, const char* end) {
	(void)end;
	tcPendingAdd = *start;
}

void tc_term(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingAdd) {
		TCType right = tcTypePop(), left = tcTypePop();
		

 
		int leftVoidPtr = tcIsPointer(left) && !tcIsPointer(tcPointee(left)) && tcPointee(left).base == 'v';
		int rightVoidPtr = tcIsPointer(right) && !tcIsPointer(tcPointee(right)) && tcPointee(right).base == 'v';
		if (tcIsPointer(left) && tcIsInteger(right) && !leftVoidPtr) {
			printf("P%s %c\n", tcPendingAdd == '+' ? "ADD" : "SUB", tcTypeTag(tcPointee(left))); tcTypePush(left);
		} else if (tcPendingAdd == '+' && tcIsInteger(left) && tcIsPointer(right) && !rightVoidPtr) {
			printf("IPADD %c\n", tcTypeTag(tcPointee(right))); tcTypePush(right);
		} else if (tcPendingAdd == '-' && tcIsPointer(left) && tcIsPointer(right) && tcSameType(left, right) && !leftVoidPtr) {
			printf("PDIFF %c\n", tcTypeTag(tcPointee(left))); tcTypePush(tcMakeType('i', 0));
		} else if (tcIsInteger(left) && tcIsInteger(right)) {
			tcTypePush(tcPromoteInteger(left, right)); printf("%s", tcPendingAdd == '+' ? "ADD\n" : "SUB\n");
		} else if (leftVoidPtr || rightVoidPtr) {
			tcErrAt(start); fprintf(stderr, "arithmetic on void* is not supported\n"); actionErrors++; tcTypePush(left);
		} else {
			tcTypeError("arithmetic", left, right); tcTypePush(left);
		}
		tcPendingAdd = 0;
	}
}








 
void tc_parenbegin(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcParenDepth >= 64) { tcErrAt(start); fprintf(stderr, "parenthesis nesting too deep\n"); actionErrors++; return; }
	tcParenSavedAdd[tcParenDepth] = tcPendingAdd;
	tcParenSavedMul[tcParenDepth] = tcPendingMul;
	tcParenSavedRel0[tcParenDepth] = tcRel0;
	tcParenSavedRel1[tcParenDepth] = tcRel1;
	
 
	tcParenSavedTgtSlot[tcParenDepth] = tcTargetSlot;
	tcCopy(tcParenSavedTgtGlobal[tcParenDepth], tcTargetGlobal, tcTargetGlobal + strlen(tcTargetGlobal));
	tcParenSavedTgtIsGlobal[tcParenDepth] = tcTargetIsGlobal;
	tcParenSavedTgtType[tcParenDepth] = tcTargetType;
	tcParenSavedTgtIsArray[tcParenDepth] = tcTargetIsArray;
	tcParenSavedTgtIndirect[tcParenDepth] = tcTargetIndirect;
	tcCopy(tcParenSavedAssignOp[tcParenDepth], tcAssignOp, tcAssignOp + strlen(tcAssignOp));
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcParenDepth++;
}


 
void tc_parenend(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcParenDepth <= 0) { tcErrAt(start); fprintf(stderr, "parenthesis-frame mismatch\n"); actionErrors++; return; }
	tcParenDepth--;
	tcPendingAdd = tcParenSavedAdd[tcParenDepth];
	tcPendingMul = tcParenSavedMul[tcParenDepth];
	tcRel0 = tcParenSavedRel0[tcParenDepth];
	tcRel1 = tcParenSavedRel1[tcParenDepth];
	tcTargetSlot = tcParenSavedTgtSlot[tcParenDepth];
	tcCopy(tcTargetGlobal, tcParenSavedTgtGlobal[tcParenDepth],
	       tcParenSavedTgtGlobal[tcParenDepth] + strlen(tcParenSavedTgtGlobal[tcParenDepth]));
	tcTargetIsGlobal = tcParenSavedTgtIsGlobal[tcParenDepth];
	tcTargetType = tcParenSavedTgtType[tcParenDepth];
	tcTargetIsArray = tcParenSavedTgtIsArray[tcParenDepth];
	tcTargetIndirect = tcParenSavedTgtIndirect[tcParenDepth];
	tcCopy(tcAssignOp, tcParenSavedAssignOp[tcParenDepth],
	       tcParenSavedAssignOp[tcParenDepth] + strlen(tcParenSavedAssignOp[tcParenDepth]));
}













 
void tc_commavalue(const char* start, const char* end) {
	(void)start; (void)end;
	tcCommaHasValue = 1;
}

void tc_commaassign(const char* start, const char* end) {
	



 
	(void)start; (void)end;
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcCompoundAssign();
	tcAssignStore(1);
	tcCommaHasValue = 1;
}

void tc_commadrop(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcCommaHasValue) {
		printf("DROP\n");
		(void)tcTypePop();
	}
	tcCommaHasValue = 0;
}

void tc_commaend(const char* start, const char* end) {
	(void)start; (void)end;
	



 
	if (!tcCommaHasValue) {
		actionErrors++;
		tcErrAt(start); fprintf(stderr, "a comma expression must end in a value, not an assignment\n");
		tcTypePush(tcBadType());
		tcCommaHasValue = 1;
	}
}

void tc_mulop(const char* start, const char* end) {
	(void)end;
	tcPendingMul = *start;
}

void tc_factor(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingMul) {
		TCType right = tcTypePop(), left = tcTypePop();
		if (!tcIsInteger(left) || !tcIsInteger(right)) tcTypeError("arithmetic", tcMakeType('i', 0), !tcIsInteger(left) ? left : right);
		tcTypePush(tcPromoteInteger(left, right));
		printf("%s", tcPendingMul == '*' ? "MUL\n" : tcPendingMul == '%' ? (left.base == 'u' || right.base == 'u') ? "UMOD\n" : "MOD\n" : (left.base == 'u' || right.base == 'u') ? "UDIV\n" : "DIV\n");
		tcPendingMul = 0;
	}
}

void tc_relop(const char* start, const char* end) {
	tcRel0 = start[0];
	tcRel1 = (end - start > 1) ? start[1] : 0;
}

void tc_expr(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcRel0) {
		TCType right, left; int pointerCompare, leftInteger, rightInteger, leftBool, rightBool;
		do { if (tcValueDepth > 0) { tcValueDepth--; right.base = tcValueTypes[tcValueDepth].base; right.pointers = tcValueTypes[tcValueDepth].pointers; right.structId = tcValueTypes[tcValueDepth].structId; right.pointeeConst = tcValueTypes[tcValueDepth].pointeeConst; } else { right.base = '?'; right.pointers = 0; right.structId = 0; right.pointeeConst = 0; actionErrors++; } } while (0); do { if (tcValueDepth > 0) { tcValueDepth--; left.base = tcValueTypes[tcValueDepth].base; left.pointers = tcValueTypes[tcValueDepth].pointers; left.structId = tcValueTypes[tcValueDepth].structId; left.pointeeConst = tcValueTypes[tcValueDepth].pointeeConst; } else { left.base = '?'; left.pointers = 0; left.structId = 0; left.pointeeConst = 0; actionErrors++; } } while (0);
		pointerCompare = tcIsPointer(left) || tcIsPointer(right);
		leftInteger = !left.pointers && (left.base == 'i' || left.base == 'u' || left.base == 'c' || left.base == 'z');
		rightInteger = !right.pointers && (right.base == 'i' || right.base == 'u' || right.base == 'c' || right.base == 'z');
		leftBool = !left.pointers && left.base == 'b';
		rightBool = !right.pointers && right.base == 'b';
		if (pointerCompare && !(tcSameType(left, right) || (tcIsPointer(left) && right.base == 'z' && !right.pointers) || (tcIsPointer(right) && left.base == 'z' && !left.pointers))) tcTypeError("pointer comparison", left, right);
		else if (!pointerCompare && !((leftInteger || leftBool) && (rightInteger || rightBool))) tcTypeError("comparison", left, right);
		do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)('b'); tcValueTypes[tcValueDepth].pointers = (unsigned char)(0); tcValueTypes[tcValueDepth].structId = (unsigned char)(0); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(0); tcValueDepth++; } else actionErrors++; } while (0);
		if (pointerCompare) {
			if (tcRel1 == '=' && tcRel0 == '=') printf("PCMPEQ\n");
			else if (tcRel1 == '=' && tcRel0 == '!') printf("PCMPNE\n");
			else if (tcRel0 == '<') printf(tcRel1 == '=' ? "PCMPLE\n" : "PCMPLT\n");
			else if (tcRel0 == '>') printf(tcRel1 == '=' ? "PCMPGE\n" : "PCMPGT\n");
		} else if (tcRel1 == '=') {
			if (tcRel0 == '<') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPULE\n" : "CMPLE\n");
			else if (tcRel0 == '>') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPUGE\n" : "CMPGE\n");
			else if (tcRel0 == '=') printf("CMPEQ\n");
			else if (tcRel0 == '!') printf("CMPNE\n");
		} else {
			if (tcRel0 == '<') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPULT\n" : "CMPLT\n");
			else if (tcRel0 == '>') printf("%s", left.base == 'u' || right.base == 'u' ? "CMPUGT\n" : "CMPGT\n");
		}
		tcRel0 = 0; tcRel1 = 0;
	}
}

void tc_target(const char* start, const char* end) {
	const char* nameEnd;
	int global;
	 
	tcPrevTargetSlot = tcTargetSlot;
	tcPrevTargetIsGlobal = tcTargetIsGlobal;
	tcPrevTargetType = tcTargetType;
	tcPrevTargetIsArray = tcTargetIsArray;
	tcPrevTargetIndirect = tcTargetIndirect;
	tcCopy(tcPrevTargetGlobal, tcTargetGlobal, tcTargetGlobal + strlen(tcTargetGlobal));

	nameEnd = tcNameEnd(start, end);
	tcTargetIsArray = nameEnd < end;
	tcTargetSlot = tcLookupLocal(start, nameEnd);
	tcTargetIsGlobal = 0; tcTargetIndirect = 0;
	tcTargetType = tcTargetSlot >= 0 ? tcLocalType(tcTargetSlot) : tcMakeType('i', 0);
	if (tcTargetSlot >= 0 && tcLocalConst[tcTargetSlot]) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	


 
	{
		int targetGlobal = tcTargetSlot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
		TCType targetGlobalType = targetGlobal >= 0 ? tcGlobalTypes[targetGlobal] : tcMakeType('i', 0);
		TCType targetGlobalPointee = targetGlobalType;
		if (targetGlobalPointee.pointers) targetGlobalPointee.pointers--; else targetGlobalPointee = tcMakeType('i', 0);
		if (tcTargetIsArray && *nameEnd == '[' &&
		    !(tcTargetSlot >= 0 && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') &&
		    
 
		    !(tcTargetSlot >= 0 && tcTargetType.pointers && tcTargetType.base == 's') &&
		    !(targetGlobal >= 0 && tcGlobalArrayLen[targetGlobal] > 0 && targetGlobalType.base == 's') &&
		    !(targetGlobal >= 0 && targetGlobalType.pointers && targetGlobalPointee.base == 's')) {
			const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
			if (afterIdx < end && *afterIdx == '.') {
				tcErrAt(start); fprintf(stderr, "indexed variable followed by a member access is only supported for a fixed array of structs, or a pointer to struct, in this version\n");
				actionErrors++; return;
			}
		}
	}
	
 
	

 
	if (tcTargetIsArray && *nameEnd == '-' && nameEnd + 1 < end && nameEnd[1] == '>') {
		int gslot = tcTargetSlot < 0 ? tcLookupGlobal(start, nameEnd) : -1;
		TCType pt;
		const char* fieldStart = nameEnd + 2; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int sid, fi;
		

 
		if (tcTargetSlot >= 0) pt = tcTargetType;
		else if (gslot >= 0) pt = tcGlobalTypes[gslot];
		else goto tcArrowSkipT;
		if (!pt.pointers || pt.base != 's') goto tcArrowSkipT;
		sid = pt.structId - 1;
		fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		






 
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		{
			int chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			printf("PUSH %d\n", tcStructFieldOffset[sid][fi]);
			if (tcTargetSlot >= 0) printf("LOADP %d\n", tcTargetSlot); else printf("LOADGP %s\n", tcGlobalNames[gslot]);
			printf("IPADD c\n");
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (fieldEnd < end && *fieldEnd == '[') {
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
		} else if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	tcArrowSkipT: ;
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcTargetType.pointers && tcTargetType.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcTargetType.structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			






 
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (chain) tcStashChainedIndex();
			printf("LOADP %d\nIPADDN %d\nPUSH %d\nPADD c\n", tcTargetSlot, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	

 
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '[' && tcLocalArrayLen[tcTargetSlot] > 0 && tcTargetType.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			int sid = tcTargetType.structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			






 
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (tcCheckNDIndex(tcLocalArrayNDims[tcTargetSlot], tcLocalArrayDims[tcTargetSlot], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcLocalArrayLen[tcTargetSlot]);
			if (chain) tcStashChainedIndex();
			printf("PUSHADDR L %d\nIPADDN %d\nPUSH %d\nPADD c\n", tcTargetSlot, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot >= 0 && tcTargetIsArray && *nameEnd == '.' && tcTargetType.base == 's') {
		int sid = tcTargetType.structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		






 
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		

 
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			if (tcLocalStructByAddr[tcTargetSlot])
				printf("PUSH %d\nLOADP %d\nIPADD c\n", tcStructFieldOffset[sid][fi], tcTargetSlot);
			else
				printf("PUSH %d\nPUSHADDR L %d\nIPADD c\n", tcStructFieldOffset[sid][fi], tcTargetSlot);
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (hasIndex) {
			
 
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	

 
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcGlobalTypes[global].pointers) {
		TCType globalPointee = tcGlobalTypes[global];
		globalPointee.pointers--;
		if (globalPointee.base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = globalPointee.structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			






 
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "ptr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (chain) tcStashChainedIndex();
			printf("LOADGP %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '[' &&
	    tcGlobalArrayLen[global] > 0 && tcGlobalTypes[global].base == 's') {
		const char* afterIdx = tcSkipAllIndexes(nameEnd, end);
		if (afterIdx < end && *afterIdx == '.') {
			char gname[32]; int sid = tcGlobalType(global).structId - 1;
			const char* fieldStart = afterIdx + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
			int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
			int structSize = tcStructByteSize[sid];
			int chain;
			tcCopy(gname, start, nameEnd);
			if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
			






 
			if (tcStructFieldConst[sid][fi]) {
				tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
				actionErrors++;
			}
			chain = fieldEnd < end && *fieldEnd == '[' && tcCountTopIndexes(fieldEnd, end) == 1 && tcStructFieldArrayLen[sid][fi] > 0;
			if (fieldEnd < end && *fieldEnd == '[' && !chain) {
				tcErrAt(start); fprintf(stderr, "arr[i].field[j] not supported in this version\n"); actionErrors++; return;
			}
			if (tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, afterIdx)) != 1) {
				tcErrAt(start); fprintf(stderr, "member access requires a complete struct-array index\n"); actionErrors++; return;
			}
			tcCheckConstIndex(nameEnd, afterIdx, tcGlobalArrayLen[global]);
			if (chain) tcStashChainedIndex();
			printf("PUSHADDR G %s\nIPADDN %d\nPUSH %d\nPADD c\n", gname, structSize, tcStructFieldOffset[sid][fi]);
			if (chain) { tcTargetType = tcEmitStashedFieldIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
	}
	if (tcTargetSlot < 0 && (global = tcLookupGlobal(start, nameEnd)) >= 0 && tcTargetIsArray && *nameEnd == '.' &&
	    tcGlobalTypes[global].base == 's') {
		char gname[32]; int sid = tcGlobalTypes[global].structId - 1;
		const char* fieldStart = nameEnd + 1; const char* fieldEnd = tcWordEnd(fieldStart, end);
		int fi = tcLookupStructField(sid, fieldStart, fieldEnd);
		int hasIndex = fieldEnd < end && *fieldEnd == '[';
		tcCopy(gname, start, nameEnd);
		if (fi < 0) { tcErrAt(start); fprintf(stderr, "unknown struct field '%.*s'\n", (int)(fieldEnd - fieldStart), fieldStart); actionErrors++; return; }
		






 
		if (tcStructFieldConst[sid][fi]) {
			tcErrAt(start); fprintf(stderr, "cannot assign to const struct field\n");
			actionErrors++;
		}
		{
			int chain = hasIndex && tcCountTopIndexes(fieldEnd, end) == 2 && tcStructFieldRowLen[sid][fi] > 0;
			if (chain) tcStashChainedIndex();
			printf("PUSH %d\nPUSHADDR G %s\nIPADD c\n", tcStructFieldOffset[sid][fi], gname);
			if (chain) { tcTargetType = tcEmitFieldRowColIndex(sid, fi, 0); tcTargetIndirect = 1; return; }
		}
		if (hasIndex) {
			









 
			if (tcCountTopIndexes(fieldEnd, end) > 1) {
				tcErrAt(start); fprintf(stderr, "chained indexing of a struct field (field[i][j]) not supported in this version\n");
				actionErrors++; return;
			}
			if (!tcStructFieldArrayLen[sid][fi]) {
				 
				if (tcIsPointer(tcStructFieldTypes[sid][fi])) {
								tcTargetType = tcEmitPtrFieldIndex(sid, fi, 0);
					tcTargetIndirect = 1;
					return;
				}
				tcErrAt(start); fprintf(stderr, "scalar struct field cannot be indexed\n"); actionErrors++; return;
			}
			tcCheckConstIndex(fieldEnd, end, tcStructFieldArrayLen[sid][fi]);
			tcEmitFieldIndexStep(sid, fi);
			tcTargetType = tcStructFieldTypes[sid][fi];
			tcTargetIndirect = 1;
			return;
		}
		if (tcStructFieldArrayLen[sid][fi] > 0) {
			tcErrAt(start); fprintf(stderr, "cannot assign to array field\n"); actionErrors++;
		}
		tcTargetType = tcStructFieldTypes[sid][fi];
		tcTargetIndirect = 1;
		return;
	}
	if (tcTargetSlot < 0 && tcLookupGlobal(start, nameEnd) >= 0) {
		int global = tcLookupGlobal(start, nameEnd);
		tcCopy(tcTargetGlobal, start, nameEnd);
		tcTargetIsGlobal = 1;
		tcTargetType = tcGlobalTypes[global];
		if (tcGlobalConst[global]) { tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++; }
		if (tcTargetIsArray && tcGlobalArrayLen[global]) {
			tcCheckNDIndex(tcGlobalArrayNDims[global], tcGlobalArrayDims[global], tcCountTopIndexes(nameEnd, end));
			tcCheckConstIndex(start, end, tcGlobalArrayLen[global]);
		}
		else if (tcTargetIsArray && tcIsPointer(tcTargetType)) {
			

 
			if (tcTargetType.pointeeConst && tcTargetType.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
			if (tcTargetType.pointers) tcTargetType.pointers--; else tcTargetType = tcMakeType('i', 0);
			if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
				tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; tcTargetType = tcMakeType('i', 0);
			}
			tcTargetIndirect = 1; printf("LOADGP %s\nPTRINDEX %c\n", tcTargetGlobal, tcTypeTag(tcTargetType));
		} else if (!!tcGlobalArrayLen[global] != tcTargetIsArray) { tcErrAt(start); fprintf(stderr, "array index mismatch\n"); actionErrors++; }
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcLocalArrayLen[tcTargetSlot]) {
		tcCheckNDIndex(tcLocalArrayNDims[tcTargetSlot], tcLocalArrayDims[tcTargetSlot], tcCountTopIndexes(nameEnd, end));
		tcCheckConstIndex(start, end, tcLocalArrayLen[tcTargetSlot]);
	} else if (tcTargetSlot >= 0 && tcTargetIsArray && tcIsPointer(tcTargetType)) {
		if (tcTargetType.pointeeConst && tcTargetType.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
		if (tcTargetType.pointers) tcTargetType.pointers--; else tcTargetType = tcMakeType('i', 0);
		if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
			tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++; tcTargetType = tcMakeType('i', 0);
		}
		tcTargetIndirect = 1; printf("LOADP %d\nPTRINDEX %c\n", tcTargetSlot, tcTypeTag(tcTargetType));
	} else if (tcTargetSlot >= 0 && tcTargetIsArray) {
		tcErrAt(start); fprintf(stderr, "array index mismatch\n"); actionErrors++;
	}
}

void tc_indirecttarget(const char* start, const char* end) {
	TCType pointer = tcTypePop(); (void)start; (void)end;
	tcTargetSlot = -1; tcTargetIsGlobal = 0; tcTargetIsArray = 0; tcTargetIndirect = 1;
	if (!tcIsPointer(pointer)) { tcTypeError("indirect assignment", tcPointerTo(tcMakeType('i', 0)), pointer); tcTargetType = tcMakeType('i', 0); return; }
	if (pointer.pointeeConst && pointer.pointers == 1) { tcErrAt(start); fprintf(stderr, "cannot assign through pointer to const\n"); actionErrors++; }
	tcTargetType = tcPointee(pointer);
	if (!tcIsPointer(tcTargetType) && tcTargetType.base == 'v') {
		tcErrAt(start); fprintf(stderr, "cannot dereference void*\n"); actionErrors++;
		tcTargetType = tcMakeType('i', 0);
	}
}

void tc_assignop(const char* start, const char* end) {
	int n = 0; const char* p;
	for (p = start; p < end && n < 2; p++) tcAssignOp[n++] = *p;
	tcAssignOp[n] = 0;
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcLoadTarget();
}







 
void tc_chainassign(const char* start, const char* end) {
	TCType got; char tag;
	(void)start; (void)end;
	tcChainHandled = 1;
	

 
	if (!tcTargetIndirect && !tcTargetIsArray && (tcPrevTargetIndirect || tcPrevTargetIsArray)) {
		got = tcTypePop();
		if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
		if (!tcCompatible(tcPrevTargetType, got)) tcTypeError("assignment", tcPrevTargetType, got);
		printf("DUP\n");
		tag = tcTypeTag(tcTargetType);
		if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
		else printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
		tag = tcTypeTag(tcPrevTargetType);
		if (tcPrevTargetIndirect) printf("STOREIND %c\n", tag);
		else if (tcPrevTargetIsGlobal) printf("STOREIDX G %s %c\n", tcPrevTargetGlobal, tag);
		else printf("STOREIDX L %d %c\n", tcPrevTargetSlot, tag);
		return;
	}
	if (tcTargetIndirect || tcTargetIsArray || tcPrevTargetIndirect || tcPrevTargetIsArray) {
		tcErrAt(start); fprintf(stderr, "chained assignment is only supported for plain variables in this version\n");
		actionErrors++; (void)tcTypePop(); return;
	}
	got = tcTypePop();
	if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
	if (!tcCompatible(tcPrevTargetType, got)) tcTypeError("assignment", tcPrevTargetType, got);
	printf("DUP\n");
	tag = tcTypeTag(tcTargetType);
	if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
	else printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
	tag = tcTypeTag(tcPrevTargetType);
	if (tcPrevTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcPrevTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcPrevTargetGlobal);
	else printf("STORE%s %d\n", tcIsPointer(tcPrevTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcPrevTargetSlot);
}

void tc_assign(const char* start, const char* end) {
	const char* p; int hasAssign = 0;
	 
	if (tcChainHandled) { tcChainHandled = 0; return; }
	for (p = start; p < end; p++) if (*p == '=') { hasAssign = 1; break; }
	if (!hasAssign) { if (!tcLastWasPrint) { printf("DROP\n"); (void)tcTypePop(); } return; }
	if (!(tcAssignOp[0] == '=' && tcAssignOp[1] == 0)) tcCompoundAssign();
	tcAssignStore(0);
}



 







 

 
static void tcDeclStructCopyScratch(void) {
	if (!tcStructCopyScratchDeclared) {
		printf("GLOBAL __structCopySrc 0 i 1\n");
		printf("GLOBAL __structCopyDst 0 i 1\n");
		tcStructCopyScratchDeclared = 1;
	}
}

 
static void tcEmitStructCopyDyn(int size) {
	int off;
	for (off = 0; off < size; off++) {
		printf("PUSH %d\nLOADGP __structCopyDst\nIPADD c\n", off);
		printf("PUSH %d\nLOADGP __structCopySrc\nIPADD c\nLOADIND c\n", off);
		printf("STOREIND c\n");
	}
}
static void tcEmitStructCopy(int dstSlot, const char* dstGlobal, int size) {
	int off;
	tcDeclStructCopyScratch();
	{
	printf("STOREGP __structCopySrc\n");
	for (off = 0; off < size; off++) {
		printf("PUSH %d\n", off);
		if (dstGlobal) printf("ADDRG %s\n", dstGlobal);
		else printf("PUSHADDR L %d\n", dstSlot);
		printf("IPADD c\n");
		printf("PUSH %d\nLOADGP __structCopySrc\nIPADD c\nLOADIND c\n", off);
		printf("STOREIND c\n");
	}
	}
}
static void tcAssignStore(int leaveValue) {
	TCType got; char tag;
	got = tcTypePop();
	if (!tcCompatible(tcTargetType, got)) tcTypeError("assignment", tcTargetType, got);
	

 
	if (tcTargetType.base == 's' && !tcTargetType.pointers && !tcTargetIndirect) {
		int sid = tcTargetType.structId - 1;
		if (sid < 0 || sid >= tcStructCount) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment to an unknown struct type\n");
			actionErrors++; return;
		}
		if (leaveValue) {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment inside an expression is not supported\n");
			actionErrors++; return;
		}
		if (tcTargetIsArray) {
			



 
			tcDeclStructCopyScratch();
			printf("STOREGP __structCopySrc\n");
			if (tcTargetIsGlobal) printf("PUSHADDR G %s\n", tcTargetGlobal);
			else if (tcTargetSlot >= 0) printf("PUSHADDR L %d\n", tcTargetSlot);
			else {
				tcErrAt(parserActionAt); fprintf(stderr, "indexed struct assignment without a target\n");
				actionErrors++; return;
			}
			printf("IPADDN %d\n", tcStructByteSize[sid]);
			printf("STOREGP __structCopyDst\n");
			tcEmitStructCopyDyn(tcStructByteSize[sid]);
		}
		else if (tcTargetIsGlobal) tcEmitStructCopy(-1, tcTargetGlobal, tcStructByteSize[sid]);
		else if (tcTargetSlot >= 0) tcEmitStructCopy(tcTargetSlot, 0, tcStructByteSize[sid]);
		else {
			tcErrAt(parserActionAt); fprintf(stderr, "struct assignment without a target\n");
			actionErrors++;
		}
		return;
	}
	tag = tcTypeTag(tcTargetType);
	



 
	if (leaveValue && !tcTargetIndirect && !tcTargetIsArray) printf("DUP\n");
	if (tcTargetIndirect) printf("STOREIND%s %c\n", leaveValue ? "KEEP" : "", tag);
	else if (tcTargetIsGlobal && tcTargetIsArray) printf("STOREIDX%s G %s %c\n", leaveValue ? "KEEP" : "", tcTargetGlobal, tag);
	else if (tcTargetIsGlobal) printf("STOREG%s %s\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "", tcTargetGlobal);
	else if (tcTargetSlot >= 0 && tcTargetIsArray) printf("STOREIDX%s L %d %c\n", leaveValue ? "KEEP" : "", tcTargetSlot, tag);
	else if (tcTargetSlot >= 0) printf("STORE%s %d\n", tcIsPointer(tcTargetType) ? "P" : tag == 'c' || tag == 'b' ? "C" : "L", tcTargetSlot);
	else { actionErrors++; tcErrAt(parserActionAt); fprintf(stderr, "unknown assignment target slot=%d global=%d array=%d indirect=%d\n", tcTargetSlot, tcTargetIsGlobal, tcTargetIsArray, tcTargetIndirect); return; }
	if (leaveValue) tcTypePush(tcTargetType);
}














 
void tc_callname(const char* start, const char* end) {
	int fpSlot; int fpGlobal; int haveFp; TCType fpType;
	if (start < end && *start == '[') {
		if (tcIndexDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "index nesting too deep\n"); return; }
		tcIndexSavedAdd[tcIndexDepth] = tcPendingAdd;
		tcIndexSavedMul[tcIndexDepth] = tcPendingMul;
		tcIndexSavedRel0[tcIndexDepth] = tcRel0;
		tcIndexSavedRel1[tcIndexDepth] = tcRel1;
		tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0; tcIndexDepth++; return;
	}
	if (tcCallDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "call nesting too deep\n"); return; }
	tcCopy(tcCallName[tcCallDepth], start, end);
	tcCallArgCount[tcCallDepth] = 0;
	tcCallFnSig[tcCallDepth] = -1;    
	












 
	if (tcLookupFunction(tcCallName[tcCallDepth]) < 0) {
		haveFp = 0;
		fpSlot = tcLookupLocal(start, end);
		if (fpSlot >= 0) {
			fpType = tcLocalTypes[fpSlot];
			if (tcIsFnPtr(fpType)) { printf("LOADP %d\n", fpSlot); haveFp = 1; }
		} else {
			fpGlobal = tcLookupGlobal(start, end);
			if (fpGlobal >= 0) {
				fpType = tcGlobalTypes[fpGlobal];
				if (tcIsFnPtr(fpType)) { printf("LOADGP %s\n", tcGlobalNames[fpGlobal]); haveFp = 1; }
			}
		}
		if (haveFp) { tcCallFnSig[tcCallDepth] = (int)fpType.structId - 1; }
	}
	 
	tcCallSavedAdd[tcCallDepth] = tcPendingAdd;
	tcCallSavedMul[tcCallDepth] = tcPendingMul;
	tcCallSavedRel0[tcCallDepth] = tcRel0;
	tcCallSavedRel1[tcCallDepth] = tcRel1;
	tcPendingAdd = 0;
	tcPendingMul = 0;
	tcRel0 = 0;
	tcRel1 = 0;
	tcCallDepth++;
}

void tc_callstmt(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcLastWasPrint) { printf("DROP\n"); (void)tcTypePop(); }
	tcLastWasPrint = 0;
}

void tc_arg(const char* start, const char* end) {
	if (start < end && *start == '[') {
		int frame; TCType indexType = tcTypePop(); if (!tcIsInteger(indexType)) tcTypeError("array index", tcMakeType('i', 0), indexType);
		if (tcIndexDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing index frame\n"); return; }
		frame = --tcIndexDepth; tcPendingAdd = tcIndexSavedAdd[frame]; tcPendingMul = tcIndexSavedMul[frame];
		tcRel0 = tcIndexSavedRel0[frame]; tcRel1 = tcIndexSavedRel1[frame]; return;
	}
	(void)end;
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	{ int f = tcLookupFunction(tcCallName[tcCallDepth - 1]); int n = tcCallArgCount[tcCallDepth - 1]; TCType got = tcTypePop();
	  if (f >= 0 && n < tcFunctionNargs[f] && !tcCompatible(tcFunctionParamTypes[f][n], got)) tcTypeError("argument", tcFunctionParamTypes[f][n], got);
	  



 
	  if (got.base == 's' && !got.pointers) {
	  	int sid = got.structId - 1;
	  	if (sid < 0 || sid >= tcStructCount) {
	  		tcErrAt(start); fprintf(stderr, "struct argument of an unknown struct type\n");
	  		actionErrors++;
	  	} else {
	  		

 
	  		char argName[40];
	  		sprintf(argName, "__structArg_%d_%d", sid, tcStructArgSeq++);
	  		printf("GARRAY %s c %d 1\n", argName, tcStructByteSize[sid]);
	  		tcEmitStructCopy(-1, argName, tcStructByteSize[sid]);
	  		printf("ADDRG %s\n", argName);
	  	}
	  } }
	tcCallArgCount[tcCallDepth - 1]++;
}

void tc_call(const char* start, const char* end) {
	int frame; int sig;
	(void)start; (void)end;
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	



 
	if (tcCallFnSig[frame] >= 0) {
		sig = tcCallFnSig[frame];
		if (tcFnSigNargs[sig] != tcCallArgCount[frame]) {
			actionErrors++; tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFnSigNargs[sig], tcCallArgCount[frame]);
		}
		printf("CALLIND%s %d\n", tcIsPointer(tcFnSigRet[sig]) ? "P" : "", tcCallArgCount[frame]);
		tcTypePush(tcFnSigRet[sig]);
		return;
	}
	if (tcEq(tcCallName[frame], "putint")) {
		printf("PRINT\n");
		tcLastWasPrint = 1;
	} else if (tcEq(tcCallName[frame], "putuint")) {
		printf("PRINTU\n");
		tcLastWasPrint = 1;
	} else if (tcEq(tcCallName[frame], "putchar")) {
		printf("PRINTC\n");
		tcLastWasPrint = 1;
	} else {
		int f = tcLookupFunction(tcCallName[frame]);
		if (f < 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "unknown function '%s'\n", tcCallName[frame]); return; }
		if (tcFunctionIsVariadic[f] ? tcCallArgCount[frame] < tcFunctionNargs[f] : tcFunctionNargs[f] != tcCallArgCount[frame]) {
			actionErrors++; tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFunctionNargs[f], tcCallArgCount[frame]); return;
		}
		if (tcFunctionIsExternal[f]) {
			






 
			printf("%s %s %d %d\n", tcIsPointer(tcFunctionReturnTypes[f]) ? "CALLEXTP" : "CALLEXT",
				tcCallName[frame], tcCallArgCount[frame], tcFunctionNargs[f]);
		} else {
			printf("%s %s %d\n", tcIsPointer(tcFunctionReturnTypes[f]) ? "CALLP" : "CALL", tcCallName[frame], tcCallArgCount[frame]);
		}
		if (!tcIsPointer(tcFunctionReturnTypes[f]) && tcFunctionReturnTypes[f].base == 'c') printf("NARROWC\n");
		tcTypePush(tcFunctionReturnTypes[f]);
		tcLastWasPrint = 0;
	}
}

void tc_whileend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('w')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1],
	       tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_retval(const char* start, const char* end) {
	(void)start; (void)end;
	tcRetHasVal = 1;
	tcTypePop4(&tcRetType);
}

void tc_return(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcRetHasVal) printf("PUSH 0\n");
	if (tcRetHasVal && !tcCompatible4(&tcFuncType, &tcRetType)) tcTypeError("return", tcFuncType, tcRetType);
	



 
	if (tcRetHasVal && !tcIsPointer(tcFuncType) && tcFuncType.base == 's') {
		int sid = tcFuncType.structId - 1;
		if (sid < 0 || sid >= tcStructCount) {
			tcErrAt(start); fprintf(stderr, "return of an unknown struct type\n");
			actionErrors++;
		} else {
			char retName[32];
			sprintf(retName, "__structRet_%d", sid);
			if (!tcStructRetDeclared[sid]) {
				printf("GARRAY %s c %d 1\n", retName, tcStructByteSize[sid]);
				tcStructRetDeclared[sid] = 1;
			}
			tcEmitStructCopy(-1, retName, tcStructByteSize[sid]);
			printf("ADDRG %s\n", retName);
		}
	}
	if (!tcIsPointer(tcFuncType) && tcFuncType.base == 'c') printf("NARROWC\n");
	printf("%s\n", tcIsPointer(tcFuncType) ? "RETP" : "RET");
	tcRetHasVal = 0;
}

void tc_ifbegin(const char* start, const char* end) {
	int elseLabel, endLabel;
	(void)start; (void)end;
	elseLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('i', elseLabel, 0, endLabel, 0);
}

void tc_ifcond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("if condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('i')) return;
	printf("JZ L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_thenend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('i')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1],
	       tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_ifend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('i')) return;
	printf("LABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_whilebegin(const char* start, const char* end) {
	int topLabel, endLabel;
	(void)start; (void)end;
	topLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('w', topLabel, topLabel, endLabel, 0);
	printf("LABEL L%d\n", topLabel);
}

void tc_whilecond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("while condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('w')) return;
	printf("JZ L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
}

void tc_forbegin(const char* start, const char* end) {
	int condLabel, stepLabel, endLabel, bodyLabel;
	(void)start; (void)end;
	condLabel = tcNextLabel++;
	stepLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	bodyLabel = tcNextLabel++;
	tcPushCtrl('f', condLabel, stepLabel, endLabel, bodyLabel);
}

void tc_forsep1(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("LABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_forcond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("for condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('f')) return;
	printf("JZ L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
}

void tc_forsep2(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlExtra[tcCtrlDepth - 1], tcCtrlCont[tcCtrlDepth - 1]);
}

void tc_forclose(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlTop[tcCtrlDepth - 1], tcCtrlExtra[tcCtrlDepth - 1]);
}

void tc_forend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('f')) return;
	printf("JMP L%d\nLABEL L%d\n", tcCtrlCont[tcCtrlDepth - 1], tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_dobegin(const char* start, const char* end) {
	int bodyLabel, condLabel, endLabel;
	(void)start; (void)end;
	bodyLabel = tcNextLabel++;
	condLabel = tcNextLabel++;
	endLabel = tcNextLabel++;
	tcPushCtrl('d', bodyLabel, condLabel, endLabel, 0);
	printf("LABEL L%d\n", bodyLabel);
}

void tc_dowhiletok(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('d')) return;
	printf("LABEL L%d\n", tcCtrlCont[tcCtrlDepth - 1]);
}

void tc_docond(const char* start, const char* end) {
	TCType condition = tcTypePop(); (void)start; (void)end;
	if (!tcIsTruthy(condition)) tcTypeError("do-while condition", tcMakeType('b', 0), condition);
	if (!tcNeedCtrl('d')) return;
	printf("JNZ L%d\n", tcCtrlTop[tcCtrlDepth - 1]);
}

void tc_doend(const char* start, const char* end) {
	(void)start; (void)end;
	if (!tcNeedCtrl('d')) return;
	printf("LABEL L%d\n", tcCtrlEnd[tcCtrlDepth - 1]);
	tcCtrlDepth--;
}

void tc_break(const char* start, const char* end) {
	int frame = tcFindBreakTarget(); (void)start; (void)end;
	if (frame < 0) { tcErrAt(start); fprintf(stderr, "break outside loop or switch\n"); actionErrors++; return; }
	printf("JMP L%d\n", tcCtrlEnd[frame]);
}

void tc_continue(const char* start, const char* end) {
	int frame = tcFindLoop(); (void)start; (void)end;
	if (frame < 0) { tcErrAt(start); fprintf(stderr, "continue outside loop\n"); actionErrors++; return; }
	printf("JMP L%d\n", tcCtrlCont[frame]);
}



 
void tc_goto(const char* start, const char* end) {
	char name[32];
	int i;
	tcIdentFromSpan(name, start, end, "goto");
	if (name[0] == 0) { tcErrAt(start); fprintf(stderr, "goto without label name\n"); actionErrors++; return; }
	i = tcGotoFind(name);
	if (i < 0) return;
	tcGotoUsed[i] = 1;
	printf("JMP L%d\n", tcGotoLabel[i]);
}




 
void tc_label(const char* start, const char* end) {
	char name[32];
	int i;
	tcIdentFromSpan(name, start, end, 0);
	if (name[0] == 0) { tcErrAt(start); fprintf(stderr, "empty label name\n"); actionErrors++; return; }
	i = tcGotoFind(name);
	if (i < 0) return;
	if (tcGotoDefined[i]) {
		tcErrAt(start); fprintf(stderr, "duplicate label '%s'\n", name); actionErrors++; return;
	}
	tcGotoDefined[i] = 1;
	printf("LABEL L%d\n", tcGotoLabel[i]);
}


 
void tc_charlit(const char* start, const char* end) {
	const char* p = start;
	int v;
	if (p < end && *p == 39) p++;             
	if (p >= end) { tcErrAt(start); fprintf(stderr, "empty character literal\n"); actionErrors++; do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)('i'); tcValueTypes[tcValueDepth].pointers = (unsigned char)(0); tcValueTypes[tcValueDepth].structId = (unsigned char)(0); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(0); tcValueDepth++; } else actionErrors++; } while (0); return; }
	if (*p == 92) {                           
		p++;
		if (p >= end) { tcErrAt(start); fprintf(stderr, "incomplete character escape\n"); actionErrors++; do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)('i'); tcValueTypes[tcValueDepth].pointers = (unsigned char)(0); tcValueTypes[tcValueDepth].structId = (unsigned char)(0); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(0); tcValueDepth++; } else actionErrors++; } while (0); return; }
		if      (*p == 'n')  v = 10;
		else if (*p == 't')  v = 9;
		else if (*p == 'r')  v = 13;
		else if (*p == '0')  v = 0;
		else if (*p == 92)   v = 92;
		else { tcErrAt(start); fprintf(stderr, "unsupported character escape\n"); actionErrors++; v = 0; }
	} else {
		v = (int)(unsigned char)*p;
	}
	printf("PUSH %d\n", v);
	do { if (tcValueDepth < 256) { tcValueTypes[tcValueDepth].base = (unsigned char)('i'); tcValueTypes[tcValueDepth].pointers = (unsigned char)(0); tcValueTypes[tcValueDepth].structId = (unsigned char)(0); tcValueTypes[tcValueDepth].pointeeConst = (unsigned char)(0); tcValueDepth++; } else actionErrors++; } while (0);
}




 
void tc_voidcast(const char* start, const char* end) {
	TCType t = tcTypePop();
	(void)start; (void)end;
	if (tcIsPointer(t) || t.base != 'v') printf("DROP\n");
}






 
void tc_fnptrbegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcFnPtrRetPending = tcCurrentType;
	tcExternBuildParamCount = 0;
	tcExternIsVariadic = 0;
}




 
void tc_fnptrtypedef(const char* start, const char* end) {
	const char* p = start;
	const char* nameStart;
	const char* nameEnd;
	int k, sig;
	while (p < end && *p != '(') p++;           
	while (p < end && (*p == '(' || *p == '*' || *p == ' ' || *p == '\t')) p++;
	nameStart = p;
	while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
	                   (*p >= '0' && *p <= '9') || *p == '_')) p++;
	nameEnd = p;
	if (nameStart == nameEnd) { tcErrAt(start); fprintf(stderr, "malformed function pointer typedef\n"); actionErrors++; return; }
	if (tcExternIsVariadic) { tcErrAt(start); fprintf(stderr, "variadic function pointers are not supported\n"); actionErrors++; return; }
	if (tcTypedefCount >= 32) { tcErrAt(start); fprintf(stderr, "too many typedefs\n"); actionErrors++; return; }
	if (tcLookupTypedef(nameStart, nameEnd) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate typedef '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; return; }
	if (tcExternBuildParamCount > 8) { tcErrAt(start); fprintf(stderr, "too many function pointer parameters\n"); actionErrors++; return; }
	if (tcFnSigCount >= 16) { tcErrAt(start); fprintf(stderr, "too many function pointer signatures\n"); actionErrors++; return; }
	sig = tcFnSigCount++;
	tcFnSigRet[sig] = tcFnPtrRetPending;
	tcFnSigNargs[sig] = tcExternBuildParamCount;
	for (k = 0; k < tcExternBuildParamCount; k++) tcFnSigParams[sig][k] = tcExternBuildParamTypes[k];
	tcCopy(tcTypedefNames[tcTypedefCount], nameStart, nameEnd);
	tcTypedefTypes[tcTypedefCount] = tcMakeFnPtr(sig);
	tcTypedefCount++;
}





 
void tc_indcallbegin(const char* start, const char* end) {
	TCType callee = tcTypePop();
	(void)start; (void)end;
	if (tcCallDepth >= 64) { actionErrors++; tcErrAt(start); fprintf(stderr, "call nesting too deep\n"); return; }
	tcCallName[tcCallDepth][0] = 0;           
	tcCallArgCount[tcCallDepth] = 0;
	if (!tcIsFnPtr(callee)) {
		tcErrAt(start); fprintf(stderr, "called value is not a function pointer\n"); actionErrors++;
		tcCallFnSig[tcCallDepth] = -1;
	} else {
		tcCallFnSig[tcCallDepth] = (int)callee.structId - 1;
	}
	tcCallSavedAdd[tcCallDepth] = tcPendingAdd;
	tcCallSavedMul[tcCallDepth] = tcPendingMul;
	tcCallSavedRel0[tcCallDepth] = tcRel0;
	tcCallSavedRel1[tcCallDepth] = tcRel1;
	tcPendingAdd = 0; tcPendingMul = 0; tcRel0 = 0; tcRel1 = 0;
	tcCallDepth++;
}




 
void tc_indcall(const char* start, const char* end) {
	int frame, sig;
	(void)start; (void)end;
	if (tcCallDepth <= 0) { actionErrors++; tcErrAt(start); fprintf(stderr, "missing call frame\n"); return; }
	frame = --tcCallDepth;
	tcPendingAdd = tcCallSavedAdd[frame];
	tcPendingMul = tcCallSavedMul[frame];
	tcRel0 = tcCallSavedRel0[frame];
	tcRel1 = tcCallSavedRel1[frame];
	sig = tcCallFnSig[frame];
	if (sig < 0) { tcTypePush(tcBadType()); return; }
	if (tcFnSigNargs[sig] != tcCallArgCount[frame]) {
		tcErrAt(start); fprintf(stderr, "wrong argument count (expected %d, got %d)\n", tcFnSigNargs[sig], tcCallArgCount[frame]); actionErrors++;
	}
	printf("CALLIND%s %d\n", tcIsPointer(tcFnSigRet[sig]) ? "P" : "", tcCallArgCount[frame]);
	tcTypePush(tcFnSigRet[sig]);
}

void tc_structbegin(const char* start, const char* end) {
	tcCopy(tcStructBuildName, start, end);
	tcStructBuildFieldCount = 0;
}

void tc_anonstructbegin(const char* start, const char* end) {
	




 
	(void)start; (void)end;
	tcStructBuildFieldCount = 0;
	tcAnonStructPending = 1;
	tcBasePointers = 0;
}

void tc_structfield(const char* start, const char* end) {
	
 
	const char* p = end - 1; const char* fieldEnd; const char* fieldStart; const char* bracket;
	int arrayLen = 0; int rowLen = 0;
	while (p > start && (*p == ' ' || *p == '\t' || *p == ';')) p--;
	fieldEnd = p + 1;
	
 
	while (p > start && *(p - 1) != ' ' && *(p - 1) != '\t' && *(p - 1) != '*') p--;
	fieldStart = p;
	bracket = fieldStart;
	while (bracket < fieldEnd && *bracket != '[') bracket++;
	if (bracket < fieldEnd) {
		const char* q = bracket + 1;
		while (q < fieldEnd && *q >= '0' && *q <= '9') arrayLen = arrayLen * 10 + (*q++ - '0');
		

 
		while (q < fieldEnd && (*q == ']' || *q == ' ' || *q == '\t')) q++;
		if (q < fieldEnd && *q == '[') {
			int dim2 = 0;
			q++;
			while (q < fieldEnd && *q >= '0' && *q <= '9') dim2 = dim2 * 10 + (*q++ - '0');
			if (dim2 > 0) { rowLen = dim2; arrayLen = arrayLen * dim2; }
		}
		fieldEnd = bracket;
	}
	if (tcStructBuildFieldCount < 16) {
		tcStructBuildFieldTypes[tcStructBuildFieldCount] = tcCurrentType;
		
 
		if (tcFieldConst && tcIsPointer(tcCurrentType))
			tcStructBuildFieldTypes[tcStructBuildFieldCount].pointeeConst = 1;
		tcStructBuildFieldConst[tcStructBuildFieldCount] =
			(char)(tcFieldConst && !tcIsPointer(tcCurrentType));
		tcStructBuildFieldArrayLen[tcStructBuildFieldCount] = arrayLen;
		tcStructBuildFieldRowLen[tcStructBuildFieldCount] = rowLen;
		tcCopy(tcStructBuildFieldNames[tcStructBuildFieldCount], fieldStart, fieldEnd);
		tcStructBuildFieldCount++;
	} else { tcErrAt(start); fprintf(stderr, "too many struct fields\n"); actionErrors++; }
}





 
static int tcRegisterStruct(const char* nameStart, const char* nameEnd) {
	int i; int offset = 0; int sid = tcStructCount;
	if (tcStructCount >= 16) { tcErrAt(parserActionAt); fprintf(stderr, "too many structs\n"); actionErrors++; return -1; }
	if (tcLookupStruct(nameStart, nameEnd) >= 0) { tcErrAt(parserActionAt); fprintf(stderr, "duplicate struct\n"); actionErrors++; return -1; }
	if (tcStructBuildFieldCount == 0) { tcErrAt(parserActionAt); fprintf(stderr, "struct needs at least one field\n"); actionErrors++; return -1; }
	











 
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		TCType ft = tcStructBuildFieldTypes[i]; int elemSize, align, size;
		if (ft.base == 's' || ft.base == 'v') {
			tcErrAt(parserActionAt); fprintf(stderr, "struct field type not supported in this version\n"); actionErrors++; return -1;
		}
		







 
		if (tcIsPointer(ft) && tcStructBuildFieldRowLen[i] > 0) {
			tcErrAt(parserActionAt); fprintf(stderr, "two-dimensional pointer arrays as struct field not supported in this version\n"); actionErrors++; return -1;
		}
		elemSize = tcIsPointer(ft) ? 8 : (ft.base == 'c' || ft.base == 'b') ? 1 : 4;
		align = elemSize;
		size = tcStructBuildFieldArrayLen[i] > 0 ? elemSize * tcStructBuildFieldArrayLen[i] : elemSize;
		offset = (offset + align - 1) & ~(align - 1);
		tcStructFieldOffset[sid][i] = offset;
		offset += size;
	}
	tcStructByteSize[sid] = (offset + 3) & ~3;
	tcCopy(tcStructNames[tcStructCount], nameStart, nameEnd);
	tcStructFieldCount[tcStructCount] = tcStructBuildFieldCount;
	for (i = 0; i < tcStructBuildFieldCount; i++) {
		tcStructFieldTypes[tcStructCount][i] = tcStructBuildFieldTypes[i];
		tcStructFieldConst[tcStructCount][i] = tcStructBuildFieldConst[i];
		tcStructFieldArrayLen[tcStructCount][i] = tcStructBuildFieldArrayLen[i];
		tcStructFieldRowLen[tcStructCount][i] = tcStructBuildFieldRowLen[i];
		tcCopy(tcStructFieldNames[tcStructCount][i], tcStructBuildFieldNames[i], tcStructBuildFieldNames[i] + strlen(tcStructBuildFieldNames[i]));
	}
	tcStructCount++;
	return sid;
}

void tc_structend(const char* start, const char* end) {
	(void)start; (void)end;
	tcRegisterStruct(tcStructBuildName, tcStructBuildName + strlen(tcStructBuildName));
}









 
void tc_typedeftarget(const char* start, const char* end) {
	if (tcTypedefCount >= 32) { tcErrAt(start); fprintf(stderr, "too many typedefs\n"); actionErrors++; return; }
	if (tcLookupTypedef(start, end) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate typedef '%.*s'\n", (int)(end - start), start); actionErrors++; return; }
	if (tcAnonStructPending) {
		






 
		int sid = tcRegisterStruct(start, end);
		tcAnonStructPending = 0;
		if (sid < 0) return;
		tcCurrentType.base = 's';
		tcCurrentType.structId = (unsigned char)(sid + 1);
		tcCurrentType.pointeeConst = 0;
		tcBasePointers = 0;
	}
	tcTypedefTypes[tcTypedefCount] = tcCurrentType;
	tcCopy(tcTypedefNames[tcTypedefCount], start, end);
	tcTypedefCount++;
}

void tc_enumdecl(const char* start, const char* end) {
	 
	const char* p = start; long value = 0; const char* nameStart; const char* nameEnd;
	while (p < end && *p != ' ' && *p != '\t') p++;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	nameStart = p; nameEnd = tcWordEnd(p, end);
	if (nameEnd > nameStart) {
		if (tcEnumTypeCount < 16) tcCopy(tcEnumTypeNames[tcEnumTypeCount++], nameStart, nameEnd);
		else { tcErrAt(start); fprintf(stderr, "too many enum types\n"); actionErrors++; }
	}
	while (p < end && *p != '{') p++;
	if (p == end) return;
	p++;
	for (;;) {
		const char* ne;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		if (p < end && *p == '}') break;
		ne = tcWordEnd(p, end);
		if (ne == p) break;
		if (tcEnumConstCount >= 128) { tcErrAt(start); fprintf(stderr, "too many enum constants\n"); actionErrors++; }
		else if (tcLookupEnumConst(p, ne) >= 0) { tcErrAt(start); fprintf(stderr, "duplicate enum constant\n"); actionErrors++; }
		else {
			tcCopy(tcEnumConstNames[tcEnumConstCount], p, ne);
			tcEnumConstValues[tcEnumConstCount] = value;
			tcEnumConstCount++;
		}
		value++;
		p = ne;
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
		if (p < end && *p == ',') { p++; continue; }
		break;
	}
}

void tc_sizeof(const char* start, const char* end) {
	int size;
	const char* q;
	int ptrs;
	tc_type(start, end);
	











 
	ptrs = 0;
	for (q = start; q < end; q++) if (*q == '*') ptrs++;
	while (ptrs > 0 && tcCurrentType.pointers < 255) {
		tcCurrentType.pointers++;
		ptrs--;
	}
	if (tcCurrentType.pointers) {
		tcErrAt(start); fprintf(stderr, "sizeof of pointer types not supported in this version\n");
		actionErrors++; size = 4;
	} else if (tcCurrentType.base == 's') {
		size = tcStructByteSize[tcCurrentType.structId - 1];
	} else if (tcCurrentType.base == 'c' || tcCurrentType.base == 'b') size = 1;
	else size = 4;
	printf("PUSH %d\n", size);
	tcTypePush(tcMakeType('i', 0));
}

void tc_sizeofvar(const char* start, const char* end) {
	int slot = tcLookupLocal(start, end), global = -1; TCType t; int size; int count;
	if (slot < 0) global = tcLookupGlobal(start, end);
	if (slot < 0 && global < 0) {
		



 
		int td = tcLookupTypedef(start, end);
		int tdBase; int tdPtrs; int tdSid; int tdSize;
		if (td < 0) { tcErrAt(start); fprintf(stderr, "unknown variable in sizeof: '%.*s'\n", (int)(end - start), start); actionErrors++; tcTypePush(tcBadType()); return; }
		tdBase = tcTypedefTypes[td].base;
		tdPtrs = tcTypedefTypes[td].pointers;
		tdSid = tcTypedefTypes[td].structId;
		if (tdPtrs) {
			tcErrAt(start); fprintf(stderr, "sizeof of pointer types not supported in this version\n");
			actionErrors++; tdSize = 4;
		} else if (tdBase == 's') tdSize = tcStructByteSize[tdSid - 1];
		else if (tdBase == 'c' || tdBase == 'b') tdSize = 1;
		else tdSize = 4;
		printf("PUSH %d\n", tdSize);
		tcTypePush(tcMakeType('i', 0));
		return;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	count = slot >= 0 ? tcLocalArrayLen[slot] : tcGlobalArrayLen[global];
	if (tcIsPointer(t)) {
		tcErrAt(start); fprintf(stderr, "sizeof of pointer types not supported in this version\n");
		actionErrors++; size = 4;
	} else if (t.base == 's') {
		size = (count > 0 ? count : 1) * tcStructByteSize[t.structId - 1];
	} else {
		int elemSize = (t.base == 'c' || t.base == 'b') ? 1 : 4;
		size = count > 0 ? count * elemSize : elemSize;
	}
	printf("PUSH %d\n", size);
	tcTypePush(tcMakeType('i', 0));
}

void tc_preincdec(const char* start, const char* end) {
	int isDec = (start[0] == '-');
	const char* p = start + 2; const char* nameStart; const char* nameEnd;
	int slot, global = -1; TCType t;
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	 
	if (p < end && *p == '(') { tcDerefIncDec(p, end, isDec, 1); return; }
	if (tcWordEnd(p, end) < end && *tcWordEnd(p, end) == '[') { tcIndexIncDec(p, end, isDec, 1); return; }
	 
	{ const char* we = tcWordEnd(p, end);
	  if (we < end && (*we == '.' || (*we == '-' && we + 1 < end && we[1] == '>'))) { tcMemberIncDec(p, end, isDec, 1); return; } }
	nameStart = p; nameEnd = tcWordEnd(p, end);
	slot = tcLookupLocal(nameStart, nameEnd);
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		tcErrAt(start); fprintf(stderr, "++/-- only supported for plain int/unsigned/char variables in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 1);
}

void tc_postincdec(const char* start, const char* end) {
	const char* nameStart = start; const char* nameEnd;
	int isDec = (end[-1] == '-');
	int slot, global = -1; TCType t;
	if (start < end && *start == '(') { tcDerefIncDec(start, end, isDec, 0); return; }
	if (tcWordEnd(start, end) < end && *tcWordEnd(start, end) == '[') { tcIndexIncDec(start, end, isDec, 0); return; }
	


 
	{ const char* we = tcWordEnd(start, end);
	  if (we < end && (*we == '.' || (*we == '-' && we + 1 < end && we[1] == '>'))) { tcMemberIncDec(start, end, isDec, 0); return; } }
	nameEnd = tcWordEnd(start, end);
	slot = tcLookupLocal(nameStart, nameEnd);
	if (slot < 0) global = tcLookupGlobal(nameStart, nameEnd);
	if (slot < 0 && global < 0) { tcErrAt(start); fprintf(stderr, "unknown variable '%.*s'\n", (int)(nameEnd - nameStart), nameStart); actionErrors++; tcTypePush(tcBadType()); return; }
	if ((slot >= 0 && tcLocalConst[slot]) || (global >= 0 && tcGlobalConst[global])) {
		tcErrAt(start); fprintf(stderr, "cannot assign to const variable\n"); actionErrors++;
	}
	t = slot >= 0 ? tcLocalType(slot) : tcGlobalType(global);
	if (!tcIncDecCheck(slot, global, t)) {
		tcErrAt(start); fprintf(stderr, "++/-- only supported for plain int/unsigned/char variables in this version\n");
		actionErrors++; tcTypePush(tcBadType()); return;
	}
	tcIncDecEmit(slot, global, t, isDec, 0);
}

void tc_incdecstmt(const char* start, const char* end) {
	(void)start; (void)end;
	printf("DROP\n"); (void)tcTypePop();
}

void tc_forstep(const char* start, const char* end) {
	
 
	const char* p;
	for (p = start; p < end; p++) if (*p == '=') { tc_assign(start, end); return; }
	printf("DROP\n"); (void)tcTypePop();
}

void tc_switchbegin(const char* start, const char* end) {
	int endLabel; (void)start; (void)end;
	if (tcSwitchDepth >= 16) { tcErrAt(start); fprintf(stderr, "switch nesting too deep\n"); actionErrors++; return; }
	endLabel = tcNextLabel++;
	tcSwitchBodyLabel[tcSwitchDepth] = -1;
	tcSwitchNextLabel[tcSwitchDepth] = -1;
	tcSwitchEndLabel[tcSwitchDepth] = endLabel;
	tcSwitchGroupOpen[tcSwitchDepth] = 0;
	tcSwitchHadDefault[tcSwitchDepth] = 0;
	tcPushCtrl('s', 0, 0, endLabel, 0);
	tcSwitchDepth++;
}

void tc_switchcond(const char* start, const char* end) {
	TCType t = tcTypePop(); (void)start; (void)end;
	if (!tcIsInteger(t)) tcTypeError("switch expression", tcMakeType('i', 0), t);
}

void tc_caselabel(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; long value; const char* p = start + 4; const char* ne;
	if (d < 0) { tcErrAt(start); fprintf(stderr, "case outside switch\n"); actionErrors++; return; }
	while (p < end && (*p == ' ' || *p == '\t')) p++;
	if (*p == '-' || (*p >= '0' && *p <= '9')) {
		int neg = 0;
		if (*p == '-') { neg = 1; p++; }
		value = 0;
		while (p < end && *p >= '0' && *p <= '9') value = value * 10 + (*p++ - '0');
		if (neg) value = -value;
	} else {
		int ec;
		ne = tcWordEnd(p, end);
		ec = tcLookupEnumConst(p, ne);
		if (ec < 0) { tcErrAt(start); fprintf(stderr, "unknown case value\n"); actionErrors++; return; }
		value = tcEnumConstValues[ec];
	}
	if (!tcSwitchGroupOpen[d]) {
		tcSwitchBodyLabel[d] = tcNextLabel++;
		tcSwitchNextLabel[d] = tcNextLabel++;
		tcSwitchGroupOpen[d] = 1;
	}
	printf("DUP\nPUSH %ld\nCMPEQ\nJNZ L%d\n", value, tcSwitchBodyLabel[d]);
}

void tc_caselabelrun_end(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	printf("JMP L%d\nLABEL L%d\nDROP\n", tcSwitchNextLabel[d], tcSwitchBodyLabel[d]);
}

void tc_casegroup_end(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	printf("JMP L%d\nLABEL L%d\n", tcSwitchEndLabel[d], tcSwitchNextLabel[d]);
	tcSwitchGroupOpen[d] = 0;
}

void tc_defaultlabel(const char* start, const char* end) {
	int d = tcSwitchDepth - 1; (void)start; (void)end;
	if (d < 0) return;
	tcSwitchHadDefault[d] = 1;
	printf("DROP\n");
}

void tc_switchend(const char* start, const char* end) {
	int d; (void)start; (void)end;
	if (!tcNeedCtrl('s')) return;
	d = tcSwitchDepth - 1;
	if (!tcSwitchHadDefault[d]) printf("DROP\n");
	printf("LABEL L%d\n", tcSwitchEndLabel[d]);
	tcSwitchDepth--;
	tcCtrlDepth--;
}

void tc_castcapture(const char* start, const char* end) {
	const char* p;
	tc_type(start, end);
	

 
	for (p = start; p < end; p++) if (*p == '*') tcCurrentType = tcPointerTo(tcCurrentType);
	if (tcCastDepth < 16) tcCastStack[tcCastDepth++] = tcCurrentType;
	else { tcErrAt(start); fprintf(stderr, "cast nesting too deep\n"); actionErrors++; }
}

void tc_cast(const char* start, const char* end) {
	TCType target; TCType src = tcTypePop(); (void)start; (void)end;
	if (tcCastDepth <= 0) { tcTypePush(tcBadType()); return; }
	target = tcCastStack[--tcCastDepth];
	

















 
	if (!tcIsInteger(src) && !tcIsBool(src) && !tcIsPointer(src) && !tcIsFnPtr(src)) {
		tcErrAt(start); fprintf(stderr, "cast expects scalar operand, got ");
		tcPrintType(stderr, src); fputc('\n', stderr); actionErrors++;
		tcTypePush(target); return;
	}
	







 
	if (tcIsInteger(target) || tcIsBool(target)) {
		if (target.base == 'c') printf("NARROWC\n");
		else if (target.base == 'b') printf("PUSH 0\nCMPNE\n");
	}
	tcTypePush(target);
}

void tc_logicandop(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicBegin('&');
}

void tc_logicandend(const char* start, const char* end) {
	if (tcHasTopToken(start, end, '&', '&')) tcLogicEnd('&');
}

void tc_logicorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcLogicBegin('|');
}

void tc_logicorend(const char* start, const char* end) {
	if (tcHasTopToken(start, end, '|', '|')) tcLogicEnd('|');
}

void tc_bitandop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('&');
}

void tc_bitandend(const char* start, const char* end) {
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '&' && tcHasTopChar(start, end, '&')) tcBitEnd('&');
}

void tc_bitxorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('^');
}

void tc_bitxorend(const char* start, const char* end) {
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '^' && tcHasTopChar(start, end, '^')) tcBitEnd('^');
}

void tc_bitorop(const char* start, const char* end) {
	(void)start; (void)end;
	tcBitBegin('|');
}

void tc_bitorend(const char* start, const char* end) {
	if (tcBitDepth > 0 && tcBitKind[tcBitDepth - 1] == '|' && tcHasTopChar(start, end, '|')) tcBitEnd('|');
}

void tc_shiftop(const char* start, const char* end) {
	(void)end;
	tcPendingShift0 = start[0];
	tcPendingShift1 = start[1];
}

void tc_shiftrhs(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcPendingShift0 && tcPendingShift1) tcShiftEnd();
}

void tc_ternarybegin(const char* start, const char* end) {
	(void)start; (void)end;
	tcTernaryBegin();
}

void tc_ternarymiddle(const char* start, const char* end) {
	(void)start; (void)end;
	tcTernaryMiddle();
}

void tc_ternaryend(const char* start, const char* end) {
	





 
	if (tcHasTopChar(start, end, '?')) tcTernaryEnd();
}

 
void tc_blockopen(const char* start, const char* end) {
	(void)start; (void)end;
	if (tcScopeDepth >= 64) {
		tcErrAt(start); fprintf(stderr, "blocks nested too deeply\n"); actionErrors++; return;
	}
	tcScopeMark[tcScopeDepth] = tcLocalCount;
	tcScopeDepth++;
}



 
void tc_blockend(const char* start, const char* end) {
	int mark; int i;
	(void)start; (void)end;
	if (tcScopeDepth <= 0) return;    
	tcScopeDepth--;
	mark = tcScopeMark[tcScopeDepth];
	for (i = mark; i < tcLocalCount; i++) tcLocalDead[i] = 1;
}

static void actionLogDispatch(int id, const char* start, const char* end) {
	parserActionAt = start;
	if (id == 1) { tc_externdeclend(start, end); return; }
	if (id == 2) { tc_externname(start, end); return; }
	if (id == 6) { tc_externparam(start, end); return; }
	if (id == 7) { tc_externvariadic(start, end); return; }
	if (id == 8) { tc_enumdecl(start, end); return; }
	if (id == 9) { tc_structend(start, end); return; }
	if (id == 10) { tc_structbegin(start, end); return; }
	if (id == 11) { tc_fieldconstend(start, end); return; }
	if (id == 12) { tc_fieldconst(start, end); return; }
	if (id == 13) { tc_structfield(start, end); return; }
	if (id == 17) { tc_fnptrtypedef(start, end); return; }
	if (id == 19) { tc_fnptrbegin(start, end); return; }
	if (id == 24) { tc_anonstructbegin(start, end); return; }
	if (id == 25) { tc_typedeftarget(start, end); return; }
	if (id == 27) { tc_externglobaldecl(start, end); return; }
	if (id == 28) { tc_globalend(start, end); return; }
	if (id == 30) { tc_const(start, end); return; }
	if (id == 31) { tc_static(start, end); return; }
	if (id == 42) { tc_funcend(start, end); return; }
	if (id == 44) { tc_funcbodybegin(start, end); return; }
	if (id == 45) { tc_funcdeclend(start, end); return; }
	if (id == 46) { tc_funcbegin(start, end); return; }
	if (id == 53) { tc_param(start, end); return; }
	if (id == 55) { tc_blockend(start, end); return; }
	if (id == 56) { tc_blockopen(start, end); return; }
	if (id == 59) { tc_staticlocal(start, end); return; }
	if (id == 60) { tc_staticlocalname(start, end); return; }
	if (id == 62) { tc_staticruntimeinit(start, end); return; }
	if (id == 63) { tc_switchend(start, end); return; }
	if (id == 64) { tc_switchbegin(start, end); return; }
	if (id == 65) { tc_switchcond(start, end); return; }
	if (id == 67) { tc_casegroup_end(start, end); return; }
	if (id == 68) { tc_caselabelrun_end(start, end); return; }
	if (id == 69) { tc_caselabel(start, end); return; }
	if (id == 75) { tc_defaultlabel(start, end); return; }
	if (id == 78) { tc_localdecl(start, end); return; }
	if (id == 79) { tc_varinit(start, end); return; }
	if (id == 80) { tc_arrayinitstring(start, end); return; }
	if (id == 86) { tc_assign(start, end); return; }
	if (id == 87) { tc_chainassign(start, end); return; }
	if (id == 88) { tc_assignop(start, end); return; }
	if (id == 89) { tc_callstmt(start, end); return; }
	if (id == 91) { tc_voidcast(start, end); return; }
	if (id == 93) { tc_return(start, end); return; }
	if (id == 94) { tc_retval(start, end); return; }
	if (id == 95) { tc_ifend(start, end); return; }
	if (id == 96) { tc_ifbegin(start, end); return; }
	if (id == 98) { tc_ifcond(start, end); return; }
	if (id == 99) { tc_thenend(start, end); return; }
	if (id == 101) { tc_whileend(start, end); return; }
	if (id == 102) { tc_whilebegin(start, end); return; }
	if (id == 103) { tc_whilecond(start, end); return; }
	if (id == 105) { tc_forend(start, end); return; }
	if (id == 106) { tc_forbegin(start, end); return; }
	if (id == 107) { tc_forsep1(start, end); return; }
	if (id == 108) { tc_forsep2(start, end); return; }
	if (id == 109) { tc_forclose(start, end); return; }
	if (id == 110) { tc_assign(start, end); return; }
	if (id == 111) { tc_forcond(start, end); return; }
	if (id == 112) { tc_forstep(start, end); return; }
	if (id == 114) { tc_doend(start, end); return; }
	if (id == 115) { tc_dobegin(start, end); return; }
	if (id == 116) { tc_dowhiletok(start, end); return; }
	if (id == 117) { tc_docond(start, end); return; }
	if (id == 120) { tc_break(start, end); return; }
	if (id == 121) { tc_continue(start, end); return; }
	if (id == 122) { tc_goto(start, end); return; }
	if (id == 123) { tc_label(start, end); return; }
	if (id == 125) { tc_ternaryend(start, end); return; }
	if (id == 126) { tc_parenbegin(start, end); return; }
	if (id == 127) { tc_parenend(start, end); return; }
	if (id == 128) { tc_commaend(start, end); return; }
	if (id == 129) { tc_commadrop(start, end); return; }
	if (id == 131) { tc_commaassign(start, end); return; }
	if (id == 132) { tc_commavalue(start, end); return; }
	if (id == 133) { tc_ternarybegin(start, end); return; }
	if (id == 135) { tc_ternarymiddle(start, end); return; }
	if (id == 137) { tc_logicorend(start, end); return; }
	if (id == 138) { tc_logicorop(start, end); return; }
	if (id == 139) { tc_logicandend(start, end); return; }
	if (id == 140) { tc_logicandop(start, end); return; }
	if (id == 141) { tc_bitorend(start, end); return; }
	if (id == 142) { tc_bitorop(start, end); return; }
	if (id == 143) { tc_bitxorend(start, end); return; }
	if (id == 144) { tc_bitxorop(start, end); return; }
	if (id == 145) { tc_bitandend(start, end); return; }
	if (id == 146) { tc_bitandop(start, end); return; }
	if (id == 147) { tc_expr(start, end); return; }
	if (id == 149) { tc_shiftrhs(start, end); return; }
	if (id == 150) { tc_shiftop(start, end); return; }
	if (id == 151) { tc_relop(start, end); return; }
	if (id == 153) { tc_addop(start, end); return; }
	if (id == 154) { tc_term(start, end); return; }
	if (id == 155) { tc_mulop(start, end); return; }
	if (id == 156) { tc_factor(start, end); return; }
	if (id == 157) { tc_postfixindex(start, end); return; }
	if (id == 158) { tc_string(start, end); return; }
	if (id == 159) { tc_charlit(start, end); return; }
	if (id == 167) { tc_neg(start, end); return; }
	if (id == 168) { tc_addressref(start, end); return; }
	if (id == 171) { tc_sizeof(start, end); return; }
	if (id == 173) { tc_sizeofvar(start, end); return; }
	if (id == 174) { tc_cast(start, end); return; }
	if (id == 175) { tc_castcapture(start, end); return; }
	if (id == 177) { tc_preincdec(start, end); return; }
	if (id == 178) { tc_postincdec(start, end); return; }
	if (id == 183) { tc_incdecstmt(start, end); return; }
	if (id == 184) { tc_derefref(start, end); return; }
	if (id == 185) { tc_call(start, end); return; }
	if (id == 186) { tc_callmember(start, end); return; }
	if (id == 187) { tc_indcall(start, end); return; }
	if (id == 188) { tc_indcallbegin(start, end); return; }
	if (id == 190) { tc_arg(start, end); return; }
	if (id == 192) { tc_target(start, end); return; }
	if (id == 193) { tc_indirecttarget(start, end); return; }
	if (id == 194) { tc_varref(start, end); return; }
	if (id == 195) { tc_arg(start, end); return; }
	if (id == 196) { tc_callname(start, end); return; }
	if (id == 198) { tc_defname(start, end); return; }
	if (id == 200) { tc_local(start, end); return; }
	if (id == 202) { tc_callname(start, end); return; }
	if (id == 203) { tc_type(start, end); return; }
	if (id == 207) { tc_pointerdecl(start, end); return; }
	if (id == 210) { tc_number(start, end); return; }
	if (id == 212) { tc_number(start, end); return; }
	actionErrors++;
}

static int p_program(void);
static int p_externDecl(void);
static int p_externName(void);
static int p_externParams(void);
static int p_externRealParams(void);
static int p_externParamList(void);
static int p_externParam(void);
static int p_ellipsisTok(void);
static int p_enumDecl(void);
static int p_structDecl(void);
static int p_structName(void);
static int p_structField(void);
static int p_fieldConstKw(void);
static int p_structDeclarator(void);
static int p_fieldName(void);
static int p_typedefDecl(void);
static int p_typedefDeclarator(void);
static int p_fnPtrTypedef(void);
static int p_fnPtrParams(void);
static int p_fnPtrOpen(void);
static int p_fnPtrName(void);
static int p_typedefType(void);
static int p_anonStructType(void);
static int p_structTagName(void);
static int p_anonStructOpen(void);
static int p_typedefTargetName(void);
static int p_globalDecl(void);
static int p_externGlobalDecl(void);
static int p_plainGlobalDecl(void);
static int p_globalDeclarator(void);
static int p_constKw(void);
static int p_staticKw(void);
static int p_globalInit(void);
static int p_globalStringInit(void);
static int p_arraySize(void);
static int p_arraySizeN(void);
static int p_constSize(void);
static int p_constSizeOp(void);
static int p_globalValue(void);
static int p_globalBool(void);
static int p_globalNeg(void);
static int p_globalNumber(void);
static int p_funcdef(void);
static int p_funcBody(void);
static int p_funcBodyOpen(void);
static int p_protoEnd(void);
static int p_funcHead(void);
static int p_retConstKw(void);
static int p_funcParams(void);
static int p_voidParams(void);
static int p_normalParams(void);
static int p_paramList(void);
static int p_param(void);
static int p_paramDecl(void);
static int p_paramArray(void);
static int p_block(void);
static int p_blockOpen(void);
static int p_statement(void);
static int p_unlabeledStmt(void);
static int p_staticVarDecl(void);
static int p_staticLocalName(void);
static int p_staticInit(void);
static int p_staticRuntimeInit(void);
static int p_switchStmt(void);
static int p_switchKw(void);
static int p_switchCond(void);
static int p_switchClose(void);
static int p_caseGroup(void);
static int p_caseLabelRun(void);
static int p_caseLabel(void);
static int p_caseValue(void);
static int p_caseNeg(void);
static int p_caseNumber(void);
static int p_caseBody(void);
static int p_defaultGroup(void);
static int p_defaultLabel(void);
static int p_varDecl(void);
static int p_varDeclarator(void);
static int p_localDecl(void);
static int p_varInit(void);
static int p_arrayStringInit(void);
static int p_initList(void);
static int p_initValue(void);
static int p_initBool(void);
static int p_initNeg(void);
static int p_initNumber(void);
static int p_assignStmt(void);
static int p_chainAssign(void);
static int p_assignop(void);
static int p_callStmt(void);
static int p_emptyStmt(void);
static int p_voidCastStmt(void);
static int p_voidCastOpen(void);
static int p_returnStmt(void);
static int p_retVal(void);
static int p_ifStmt(void);
static int p_ifKw(void);
static int p_elseKw(void);
static int p_ifCond(void);
static int p_thenPart(void);
static int p_elsePart(void);
static int p_whileStmt(void);
static int p_whileKw(void);
static int p_whileCond(void);
static int p_whileBody(void);
static int p_forStmt(void);
static int p_forKw(void);
static int p_forSep1(void);
static int p_forSep2(void);
static int p_forClose(void);
static int p_forInit(void);
static int p_forCond(void);
static int p_forStep(void);
static int p_forBody(void);
static int p_doStmt(void);
static int p_doKw(void);
static int p_doWhileTok(void);
static int p_doCond(void);
static int p_doClose(void);
static int p_doBody(void);
static int p_breakStmt(void);
static int p_continueStmt(void);
static int p_gotoStmt(void);
static int p_labelStmt(void);
static int p_expr(void);
static int p_conditionalExpr(void);
static int p_parenOpen(void);
static int p_parenClose(void);
static int p_commaExpr(void);
static int p_commaTok(void);
static int p_commaItem(void);
static int p_commaAssign(void);
static int p_commaValue(void);
static int p_qmark(void);
static int p_conditionalTrue(void);
static int p_colon(void);
static int p_conditionalFalse(void);
static int p_orExpr(void);
static int p_orop(void);
static int p_andExpr(void);
static int p_andop(void);
static int p_bitOrExpr(void);
static int p_bitorop(void);
static int p_bitXorExpr(void);
static int p_bitxorop(void);
static int p_bitAndExpr(void);
static int p_bitandop(void);
static int p_comparison(void);
static int p_shiftExpr(void);
static int p_shiftRhs(void);
static int p_shiftop(void);
static int p_relop(void);
static int p_addExpr(void);
static int p_addop(void);
static int p_term(void);
static int p_mulop(void);
static int p_factor(void);
static int p_postfixIndex(void);
static int p_stringLit(void);
static int p_charLit(void);
static int p_charLitBody(void);
static int p_charLitEscape(void);
static int p_charLitEscChar(void);
static int p_charLitPlain(void);
static int p_character(void);
static int p_strEscape(void);
static int p_strEscChar(void);
static int p_negFactor(void);
static int p_addressRef(void);
static int p_sizeofExpr(void);
static int p_sizeofArg(void);
static int p_sizeofType(void);
static int p_sizeofBaseType(void);
static int p_sizeofVarName(void);
static int p_castExpr(void);
static int p_castType(void);
static int p_castOperand(void);
static int p_preIncDec(void);
static int p_postIncDec(void);
static int p_memberIncTarget(void);
static int p_indexIncTarget(void);
static int p_derefIncTarget(void);
static int p_incdecOp(void);
static int p_incDecStmt(void);
static int p_derefRef(void);
static int p_call(void);
static int p_callMember(void);
static int p_indirectCall(void);
static int p_indCallOpen(void);
static int p_argList(void);
static int p_arg(void);
static int p_target(void);
static int p_directTarget(void);
static int p_indirectTarget(void);
static int p_varRef(void);
static int p_index(void);
static int p_indexOpen(void);
static int p_member(void);
static int p_defName(void);
static int p_paramName(void);
static int p_localName(void);
static int p_globalName(void);
static int p_funcName(void);
static int p_type(void);
static int p_structTypeRef(void);
static int p_enumTypeRef(void);
static int p_typedefRef(void);
static int p_pointerDecl(void);
static int p_pointerStar(void);
static int p_unsignedInt(void);
static int p_boolLit(void);
static int p_ident(void);
static int p_number(void);
static int p_hexNumber(void);
static int p_hexMark(void);
static int p_hexDigit(void);
static int p_decNumber(void);
static int p_letter(void);
static int p_digit(void);

 
static int p_program(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L1:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalDecl()) goto L4;
	goto L3;
L4:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_structDecl()) goto L5;
	goto L3;
L5:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefDecl()) goto L6;
	goto L3;
L6:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_enumDecl()) goto L7;
	goto L3;
L7:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externDecl()) goto L8;
	goto L3;
L8:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_funcdef()) goto L9;
	goto L3;
L9:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L2;
L3:	sp--;
	sp--; goto L1;
L2:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L0:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "extern", 6) != 0) goto L10;
	if (idch((unsigned char)p[6])) goto L10;
	p += 6;
	if (!p_type()) goto L10;
	if (!p_pointerDecl()) goto L10;
	if (!p_externName()) goto L10;
	if (!p_externParams()) goto L10;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L10;
	p += 1;
	actionLogPush(1, entry, p);	 
	return 1;
L10:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L11;
	actionLogPush(2, entry, p);	 
	return 1;
L11:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L14;
	goto L13;
L14:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externRealParams()) goto L15;
	goto L13;
L15:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L12;
L13:	sp--;
	return 1;
L12:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externRealParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L16;
	p += 1;
	if (!p_externParamList()) goto L16;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L16;
	p += 1;
	return 1;
L16:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externParamList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_externParam()) goto L18;
L20:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L21;
	p += 1;
	if (!p_externParam()) goto L21;
	sp--; goto L20;
L21:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L22;
	p += 1;
	if (!p_ellipsisTok()) goto L22;
	sp--; goto L23;
L22:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L23:	;
	sp--; goto L19;
L18:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L19:	;
	return 1;
L17:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externParam(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L25;
	sp--; goto L26;
L25:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L26:	;
	if (!p_type()) goto L24;
	if (!p_pointerDecl()) goto L24;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L27;
	sp--; goto L28;
L27:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L28:	;
	actionLogPush(6, entry, p);	 
	return 1;
L24:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_ellipsisTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "...", 3) != 0) goto L29;
	p += 3;
	actionLogPush(7, entry, p);	 
	return 1;
L29:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_enumDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L30;
	if (idch((unsigned char)p[4])) goto L30;
	p += 4;
	ws();
	if (!p_ident()) goto L30;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L30;
	p += 1;
	ws();
	if (!p_ident()) goto L30;
L31:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L32;
	p += 1;
	ws();
	if (!p_ident()) goto L32;
	sp--; goto L31;
L32:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L30;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L30;
	p += 1;
	actionLogPush(8, entry, p);	 
	return 1;
L30:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L33;
	if (idch((unsigned char)p[6])) goto L33;
	p += 6;
	if (!p_structName()) goto L33;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L33;
	p += 1;
	if (!p_structField()) goto L33;
L34:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L35;
	sp--; goto L34;
L35:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L33;
	p += 1;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L33;
	p += 1;
	actionLogPush(9, entry, p);	 
	return 1;
L33:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L36;
	actionLogPush(10, entry, p);	 
	return 1;
L36:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structField(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_fieldConstKw()) goto L38;
	sp--; goto L39;
L38:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L39:	;
	if (!p_type()) goto L37;
	if (!p_structDeclarator()) goto L37;
L40:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L41;
	p += 1;
	if (!p_structDeclarator()) goto L41;
	sp--; goto L40;
L41:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L37;
	p += 1;
	actionLogPush(11, entry, p);	 
	return 1;
L37:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fieldConstKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L42;
	if (idch((unsigned char)p[5])) goto L42;
	p += 5;
	actionLogPush(12, entry, p);	 
	return 1;
L42:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L43;
	if (!p_fieldName()) goto L43;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L44;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L46;
	sp--; goto L47;
L46:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L47:	;
	sp--; goto L45;
L44:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L45:	;
	actionLogPush(13, entry, p);	 
	return 1;
L43:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fieldName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L48;
	return 1;
L48:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_typedefDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "typedef", 7) != 0) goto L51;
	if (idch((unsigned char)p[7])) goto L51;
	p += 7;
	if (!p_typedefType()) goto L51;
	if (!p_typedefDeclarator()) goto L51;
L52:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L53;
	p += 1;
	if (!p_typedefDeclarator()) goto L53;
	sp--; goto L52;
L53:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L51;
	p += 1;
	goto L50;
L51:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_fnPtrTypedef()) goto L54;
	goto L50;
L54:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L49;
L50:	sp--;
	return 1;
L49:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_typedefDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L55;
	if (!p_typedefTargetName()) goto L55;
	return 1;
L55:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fnPtrTypedef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "typedef", 7) != 0) goto L56;
	if (idch((unsigned char)p[7])) goto L56;
	p += 7;
	if (!p_type()) goto L56;
	if (!p_pointerDecl()) goto L56;
	if (!p_fnPtrOpen()) goto L56;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L56;
	if (strncmp(p, "*=", 2) == 0) goto L56;	 
	p += 1;
	if (!p_fnPtrName()) goto L56;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L56;
	p += 1;
	if (!p_fnPtrParams()) goto L56;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L56;
	p += 1;
	actionLogPush(17, entry, p);	 
	return 1;
L56:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fnPtrParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L59;
	goto L58;
L59:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_externRealParams()) goto L60;
	goto L58;
L60:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L57;
L58:	sp--;
	return 1;
L57:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fnPtrOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L61;
	p += 1;
	actionLogPush(19, entry, p);	 
	return 1;
L61:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_fnPtrName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L62;
	return 1;
L62:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_typedefType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_anonStructType()) goto L65;
	goto L64;
L65:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_type()) goto L66;
	goto L64;
L66:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L63;
L64:	sp--;
	return 1;
L63:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_anonStructType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L67;
	if (idch((unsigned char)p[6])) goto L67;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structTagName()) goto L68;
	sp--; goto L69;
L68:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L69:	;
	if (!p_anonStructOpen()) goto L67;
	if (!p_structField()) goto L67;
L70:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_structField()) goto L71;
	sp--; goto L70;
L71:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L67;
	p += 1;
	return 1;
L67:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structTagName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L72;
	return 1;
L72:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_anonStructOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L73;
	p += 1;
	actionLogPush(24, entry, p);	 
	return 1;
L73:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_typedefTargetName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L74;
	actionLogPush(25, entry, p);	 
	return 1;
L74:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_externGlobalDecl()) goto L77;
	goto L76;
L77:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_plainGlobalDecl()) goto L78;
	goto L76;
L78:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L75;
L76:	sp--;
	return 1;
L75:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_externGlobalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "extern", 6) != 0) goto L79;
	if (idch((unsigned char)p[6])) goto L79;
	p += 6;
	if (!p_type()) goto L79;
	if (!p_pointerDecl()) goto L79;
	if (!p_globalName()) goto L79;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L79;
	p += 1;
	actionLogPush(27, entry, p);	 
	return 1;
L79:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_plainGlobalDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_staticKw()) goto L81;
	sp--; goto L82;
L81:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L82:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L83;
	sp--; goto L84;
L83:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L84:	;
	if (!p_type()) goto L80;
	if (!p_globalDeclarator()) goto L80;
L85:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L86;
	p += 1;
	if (!p_globalDeclarator()) goto L86;
	sp--; goto L85;
L86:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L80;
	p += 1;
	actionLogPush(28, entry, p);	 
	return 1;
L80:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L87;
	if (!p_globalName()) goto L87;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L88;
L90:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L91;
	sp--; goto L90;
L91:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L89;
L88:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L89:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L92;
	if (strncmp(p, "==", 2) == 0) goto L92;	 
	p += 1;
	if (!p_globalInit()) goto L92;
	sp--; goto L93;
L92:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L93:	;
	return 1;
L87:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_constKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L94;
	if (idch((unsigned char)p[5])) goto L94;
	p += 5;
	actionLogPush(30, entry, p);	 
	return 1;
L94:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_staticKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "static", 6) != 0) goto L95;
	if (idch((unsigned char)p[6])) goto L95;
	p += 6;
	actionLogPush(31, entry, p);	 
	return 1;
L95:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalValue()) goto L98;
	goto L97;
L98:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L99;
	goto L97;
L99:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalStringInit()) goto L100;
	goto L97;
L100:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L96;
L97:	sp--;
	return 1;
L96:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalStringInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L101;
	p += 1;
L102:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_character()) goto L103;
	sp--; goto L102;
L103:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "\"", 1) != 0) goto L101;
	p += 1;
	return 1;
L101:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_arraySize(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L104;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constSize()) goto L105;
	sp--; goto L106;
L105:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L106:	;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L104;
	p += 1;
	return 1;
L104:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_arraySizeN(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L107;
	p += 1;
	if (!p_constSize()) goto L107;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L107;
	p += 1;
	return 1;
L107:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_constSize(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_globalNumber()) goto L108;
L109:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constSizeOp()) goto L110;
	if (!p_globalNumber()) goto L110;
	sp--; goto L109;
L110:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L108:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_constSizeOp(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "+", 1) != 0) goto L113;
	if (strncmp(p, "+=", 2) == 0) goto L113;	 
	if (strncmp(p, "++", 2) == 0) goto L113;	 
	p += 1;
	goto L112;
L113:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L114;
	if (strncmp(p, "-=", 2) == 0) goto L114;	 
	if (strncmp(p, "--", 2) == 0) goto L114;	 
	if (strncmp(p, "->", 2) == 0) goto L114;	 
	p += 1;
	goto L112;
L114:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*", 1) != 0) goto L115;
	if (strncmp(p, "*=", 2) == 0) goto L115;	 
	p += 1;
	goto L112;
L115:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L111;
L112:	sp--;
	return 1;
L111:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalNumber()) goto L118;
	goto L117;
L118:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalNeg()) goto L119;
	goto L117;
L119:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_globalBool()) goto L120;
	goto L117;
L120:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L116;
L117:	sp--;
	return 1;
L116:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalBool(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L123;
	if (idch((unsigned char)p[4])) goto L123;
	p += 4;
	goto L122;
L123:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L124;
	if (idch((unsigned char)p[5])) goto L124;
	p += 5;
	goto L122;
L124:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L121;
L122:	sp--;
	return 1;
L121:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L125;
	if (strncmp(p, "-=", 2) == 0) goto L125;	 
	if (strncmp(p, "--", 2) == 0) goto L125;	 
	if (strncmp(p, "->", 2) == 0) goto L125;	 
	p += 1;
	if (!p_globalNumber()) goto L125;
	return 1;
L125:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L126;
L127:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L128;
	sp--; goto L127;
L128:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L126:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcdef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcHead()) goto L129;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_funcBody()) goto L131;
	goto L130;
L131:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_protoEnd()) goto L132;
	goto L130;
L132:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L129;
L130:	sp--;
	actionLogPush(42, entry, p);	 
	return 1;
L129:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcBodyOpen()) goto L133;
L134:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L135;
	sp--; goto L134;
L135:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L133;
	p += 1;
	return 1;
L133:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcBodyOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L136;
	p += 1;
	actionLogPush(44, entry, p);	 
	return 1;
L136:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_protoEnd(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L137;
	p += 1;
	actionLogPush(45, entry, p);	 
	return 1;
L137:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcHead(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_staticKw()) goto L139;
	sp--; goto L140;
L139:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L140:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retConstKw()) goto L141;
	sp--; goto L142;
L141:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L142:	;
	if (!p_type()) goto L138;
	if (!p_pointerDecl()) goto L138;
	if (!p_defName()) goto L138;
	if (!p_funcParams()) goto L138;
	actionLogPush(46, entry, p);	 
	return 1;
L138:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_retConstKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "const", 5) != 0) goto L143;
	if (idch((unsigned char)p[5])) goto L143;
	p += 5;
	return 1;
L143:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_voidParams()) goto L146;
	goto L145;
L146:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_normalParams()) goto L147;
	goto L145;
L147:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L144;
L145:	sp--;
	return 1;
L144:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_voidParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L148;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L148;
	if (idch((unsigned char)p[4])) goto L148;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L148;
	p += 1;
	return 1;
L148:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_normalParams(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L149;
	p += 1;
	if (!p_paramList()) goto L149;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L149;
	p += 1;
	return 1;
L149:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_paramList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_param()) goto L151;
L153:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L154;
	p += 1;
	if (!p_param()) goto L154;
	sp--; goto L153;
L154:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L152;
L151:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L152:	;
	return 1;
L150:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_param(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L156;
	sp--; goto L157;
L156:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L157:	;
	if (!p_type()) goto L155;
	if (!p_pointerDecl()) goto L155;
	if (!p_paramDecl()) goto L155;
	return 1;
L155:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_paramDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_paramName()) goto L158;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_paramArray()) goto L159;
	sp--; goto L160;
L159:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L160:	;
	actionLogPush(53, entry, p);	 
	return 1;
L158:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_paramArray(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L161;
	p += 1;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L161;
	p += 1;
	return 1;
L161:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_block(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_blockOpen()) goto L162;
L163:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_statement()) goto L164;
	sp--; goto L163;
L164:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, "}", 1) != 0) goto L162;
	p += 1;
	actionLogPush(55, entry, p);	 
	return 1;
L162:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_blockOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L165;
	p += 1;
	actionLogPush(56, entry, p);	 
	return 1;
L165:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_statement(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_labelStmt()) goto L168;
	goto L167;
L168:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_unlabeledStmt()) goto L169;
	goto L167;
L169:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L166;
L167:	sp--;
	return 1;
L166:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_unlabeledStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_ifStmt()) goto L172;
	goto L171;
L172:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_whileStmt()) goto L173;
	goto L171;
L173:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_forStmt()) goto L174;
	goto L171;
L174:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_doStmt()) goto L175;
	goto L171;
L175:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_switchStmt()) goto L176;
	goto L171;
L176:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_breakStmt()) goto L177;
	goto L171;
L177:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_continueStmt()) goto L178;
	goto L171;
L178:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_gotoStmt()) goto L179;
	goto L171;
L179:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_returnStmt()) goto L180;
	goto L171;
L180:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_block()) goto L181;
	goto L171;
L181:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_emptyStmt()) goto L182;
	goto L171;
L182:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_voidCastStmt()) goto L183;
	goto L171;
L183:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_callStmt()) goto L184;
	goto L171;
L184:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incDecStmt()) goto L185;
	goto L171;
L185:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_assignStmt()) goto L186;
	goto L171;
L186:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticVarDecl()) goto L187;
	goto L171;
L187:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varDecl()) goto L188;
	goto L171;
L188:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L170;
L171:	sp--;
	return 1;
L170:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_staticVarDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_staticKw()) goto L189;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L190;
	sp--; goto L191;
L190:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L191:	;
	if (!p_type()) goto L189;
	if (!p_pointerDecl()) goto L189;
	if (!p_staticLocalName()) goto L189;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L192;
	if (strncmp(p, "==", 2) == 0) goto L192;	 
	p += 1;
	if (!p_staticInit()) goto L192;
	sp--; goto L193;
L192:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L193:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L189;
	p += 1;
	actionLogPush(59, entry, p);	 
	return 1;
L189:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_staticLocalName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L194;
	actionLogPush(60, entry, p);	 
	return 1;
L194:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_staticInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_globalValue()) goto L197;
	goto L196;
L197:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_staticRuntimeInit()) goto L198;
	goto L196;
L198:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L195;
L196:	sp--;
	return 1;
L195:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_staticRuntimeInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L199;
	actionLogPush(62, entry, p);	 
	return 1;
L199:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_switchStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_switchKw()) goto L200;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L200;
	p += 1;
	if (!p_switchCond()) goto L200;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L200;
	p += 1;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L200;
	p += 1;
L201:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseGroup()) goto L202;
	sp--; goto L201;
L202:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_defaultGroup()) goto L203;
	sp--; goto L204;
L203:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L204:	;
	if (!p_switchClose()) goto L200;
	actionLogPush(63, entry, p);	 
	return 1;
L200:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_switchKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "switch", 6) != 0) goto L205;
	if (idch((unsigned char)p[6])) goto L205;
	p += 6;
	actionLogPush(64, entry, p);	 
	return 1;
L205:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_switchCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L206;
	actionLogPush(65, entry, p);	 
	return 1;
L206:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_switchClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L207;
	p += 1;
	return 1;
L207:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabelRun()) goto L208;
	if (!p_caseBody()) goto L208;
	actionLogPush(67, entry, p);	 
	return 1;
L208:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseLabelRun(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_caseLabel()) goto L209;
L210:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_caseLabel()) goto L211;
	sp--; goto L210;
L211:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(68, entry, p);	 
	return 1;
L209:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseLabel(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "case", 4) != 0) goto L212;
	if (idch((unsigned char)p[4])) goto L212;
	p += 4;
	if (!p_caseValue()) goto L212;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L212;
	p += 1;
	actionLogPush(69, entry, p);	 
	return 1;
L212:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_ident()) goto L215;
	goto L214;
L215:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNeg()) goto L216;
	goto L214;
L216:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_caseNumber()) goto L217;
	goto L214;
L217:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L213;
L214:	sp--;
	return 1;
L213:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L218;
	if (strncmp(p, "-=", 2) == 0) goto L218;	 
	if (strncmp(p, "--", 2) == 0) goto L218;	 
	if (strncmp(p, "->", 2) == 0) goto L218;	 
	p += 1;
	if (!p_caseNumber()) goto L218;
	return 1;
L218:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L219;
L220:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L221;
	sp--; goto L220;
L221:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L219:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_caseBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L223:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_unlabeledStmt()) goto L224;
	sp--; goto L223;
L224:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L222:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_defaultGroup(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_defaultLabel()) goto L225;
	if (!p_caseBody()) goto L225;
	return 1;
L225:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_defaultLabel(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "default", 7) != 0) goto L226;
	if (idch((unsigned char)p[7])) goto L226;
	p += 7;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L226;
	p += 1;
	actionLogPush(75, entry, p);	 
	return 1;
L226:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_varDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_constKw()) goto L228;
	sp--; goto L229;
L228:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L229:	;
	if (!p_type()) goto L227;
	if (!p_varDeclarator()) goto L227;
L230:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L231;
	p += 1;
	if (!p_varDeclarator()) goto L231;
	sp--; goto L230;
L231:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	ws();
	if (strncmp(p, ";", 1) != 0) goto L227;
	p += 1;
	return 1;
L227:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_varDeclarator(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_pointerDecl()) goto L232;
	if (!p_localDecl()) goto L232;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L233;
	if (strncmp(p, "==", 2) == 0) goto L233;	 
	p += 1;
	if (!p_varInit()) goto L233;
	sp--; goto L234;
L233:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L234:	;
	return 1;
L232:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_localDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_localName()) goto L235;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySize()) goto L236;
L238:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arraySizeN()) goto L239;
	sp--; goto L238;
L239:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L237;
L236:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L237:	;
	actionLogPush(78, entry, p);	 
	return 1;
L235:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_varInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arrayStringInit()) goto L242;
	goto L241;
L242:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_expr()) goto L243;
	goto L241;
L243:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initList()) goto L244;
	goto L241;
L244:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L240;
L241:	sp--;
	actionLogPush(79, entry, p);	 
	return 1;
L240:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_arrayStringInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_stringLit()) goto L245;
	actionLogPush(80, entry, p);	 
	return 1;
L245:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_initList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "{", 1) != 0) goto L246;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initValue()) goto L247;
L249:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L250;
	p += 1;
	if (!p_initValue()) goto L250;
	sp--; goto L249;
L250:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L248;
L247:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L248:	;
	ws();
	if (strncmp(p, "}", 1) != 0) goto L246;
	p += 1;
	return 1;
L246:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_initValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_initNumber()) goto L253;
	goto L252;
L253:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initNeg()) goto L254;
	goto L252;
L254:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_initBool()) goto L255;
	goto L252;
L255:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L251;
L252:	sp--;
	return 1;
L251:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_initBool(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L258;
	if (idch((unsigned char)p[4])) goto L258;
	p += 4;
	goto L257;
L258:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L259;
	if (idch((unsigned char)p[5])) goto L259;
	p += 5;
	goto L257;
L259:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L256;
L257:	sp--;
	return 1;
L256:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_initNeg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L260;
	if (strncmp(p, "-=", 2) == 0) goto L260;	 
	if (strncmp(p, "--", 2) == 0) goto L260;	 
	if (strncmp(p, "->", 2) == 0) goto L260;	 
	p += 1;
	if (!p_initNumber()) goto L260;
	return 1;
L260:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_initNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_digit()) goto L261;
L262:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (!p_digit()) goto L263;
	sp--; goto L262;
L263:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L261:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_assignStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_chainAssign()) goto L266;
	goto L265;
L266:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_target()) goto L267;
	if (!p_assignop()) goto L267;
	if (!p_expr()) goto L267;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L267;
	p += 1;
	goto L265;
L267:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L264;
L265:	sp--;
	actionLogPush(86, entry, p);	 
	return 1;
L264:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_chainAssign(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L268;
	if (!p_assignop()) goto L268;
	if (!p_target()) goto L268;
	if (!p_assignop()) goto L268;
	if (!p_expr()) goto L268;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L268;
	p += 1;
	actionLogPush(87, entry, p);	 
	return 1;
L268:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_assignop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "=", 1) != 0) goto L271;
	if (strncmp(p, "==", 2) == 0) goto L271;	 
	p += 1;
	goto L270;
L271:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "+=", 2) != 0) goto L272;
	p += 2;
	goto L270;
L272:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-=", 2) != 0) goto L273;
	p += 2;
	goto L270;
L273:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "*=", 2) != 0) goto L274;
	p += 2;
	goto L270;
L274:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/=", 2) != 0) goto L275;
	p += 2;
	goto L270;
L275:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%=", 2) != 0) goto L276;
	p += 2;
	goto L270;
L276:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<<=", 3) != 0) goto L277;
	p += 3;
	goto L270;
L277:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>=", 3) != 0) goto L278;
	p += 3;
	goto L270;
L278:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "&=", 2) != 0) goto L279;
	p += 2;
	goto L270;
L279:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "^=", 2) != 0) goto L280;
	p += 2;
	goto L270;
L280:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "|=", 2) != 0) goto L281;
	p += 2;
	goto L270;
L281:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L269;
L270:	sp--;
	actionLogPush(88, entry, p);	 
	return 1;
L269:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_callStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_call()) goto L284;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L284;
	p += 1;
	goto L283;
L284:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L285;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L285;
	p += 1;
	goto L283;
L285:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L282;
L283:	sp--;
	actionLogPush(89, entry, p);	 
	return 1;
L282:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_emptyStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L286;
	p += 1;
	return 1;
L286:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_voidCastStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_voidCastOpen()) goto L287;
	if (!p_expr()) goto L287;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L287;
	p += 1;
	actionLogPush(91, entry, p);	 
	return 1;
L287:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_voidCastOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L288;
	p += 1;
	ws();
	if (strncmp(p, "void", 4) != 0) goto L288;
	if (idch((unsigned char)p[4])) goto L288;
	p += 4;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L288;
	p += 1;
	return 1;
L288:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_returnStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "return", 6) != 0) goto L289;
	if (idch((unsigned char)p[6])) goto L289;
	p += 6;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_retVal()) goto L290;
	sp--; goto L291;
L290:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L291:	;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L289;
	p += 1;
	actionLogPush(93, entry, p);	 
	return 1;
L289:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_retVal(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L292;
	actionLogPush(94, entry, p);	 
	return 1;
L292:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_ifStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_ifKw()) goto L293;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L293;
	p += 1;
	if (!p_ifCond()) goto L293;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L293;
	p += 1;
	if (!p_thenPart()) goto L293;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_elseKw()) goto L294;
	if (!p_elsePart()) goto L294;
	sp--; goto L295;
L294:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L295:	;
	actionLogPush(95, entry, p);	 
	return 1;
L293:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_ifKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "if", 2) != 0) goto L296;
	if (idch((unsigned char)p[2])) goto L296;
	p += 2;
	actionLogPush(96, entry, p);	 
	return 1;
L296:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_elseKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "else", 4) != 0) goto L297;
	if (idch((unsigned char)p[4])) goto L297;
	p += 4;
	return 1;
L297:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_ifCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L298;
	actionLogPush(98, entry, p);	 
	return 1;
L298:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_thenPart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L299;
	actionLogPush(99, entry, p);	 
	return 1;
L299:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_elsePart(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L300;
	return 1;
L300:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_whileStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_whileKw()) goto L301;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L301;
	p += 1;
	if (!p_whileCond()) goto L301;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L301;
	p += 1;
	if (!p_whileBody()) goto L301;
	actionLogPush(101, entry, p);	 
	return 1;
L301:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_whileKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "while", 5) != 0) goto L302;
	if (idch((unsigned char)p[5])) goto L302;
	p += 5;
	actionLogPush(102, entry, p);	 
	return 1;
L302:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_whileCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L303;
	actionLogPush(103, entry, p);	 
	return 1;
L303:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_whileBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L304;
	return 1;
L304:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_forKw()) goto L305;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L305;
	p += 1;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forInit()) goto L306;
	sp--; goto L307;
L306:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L307:	;
	if (!p_forSep1()) goto L305;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forCond()) goto L308;
	sp--; goto L309;
L308:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L309:	;
	if (!p_forSep2()) goto L305;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_forStep()) goto L310;
	sp--; goto L311;
L310:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L311:	;
	if (!p_forClose()) goto L305;
	if (!p_forBody()) goto L305;
	actionLogPush(105, entry, p);	 
	return 1;
L305:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "for", 3) != 0) goto L312;
	if (idch((unsigned char)p[3])) goto L312;
	p += 3;
	actionLogPush(106, entry, p);	 
	return 1;
L312:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forSep1(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L313;
	p += 1;
	actionLogPush(107, entry, p);	 
	return 1;
L313:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forSep2(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L314;
	p += 1;
	actionLogPush(108, entry, p);	 
	return 1;
L314:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L315;
	p += 1;
	actionLogPush(109, entry, p);	 
	return 1;
L315:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forInit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L316;
	if (!p_assignop()) goto L316;
	if (!p_expr()) goto L316;
	actionLogPush(110, entry, p);	 
	return 1;
L316:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L317;
	actionLogPush(111, entry, p);	 
	return 1;
L317:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forStep(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_target()) goto L320;
	if (!p_assignop()) goto L320;
	if (!p_expr()) goto L320;
	goto L319;
L320:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L321;
	goto L319;
L321:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L322;
	goto L319;
L322:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L318;
L319:	sp--;
	actionLogPush(112, entry, p);	 
	return 1;
L318:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_forBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L323;
	return 1;
L323:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_doKw()) goto L324;
	if (!p_doBody()) goto L324;
	if (!p_doWhileTok()) goto L324;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L324;
	p += 1;
	if (!p_doCond()) goto L324;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L324;
	p += 1;
	if (!p_doClose()) goto L324;
	actionLogPush(114, entry, p);	 
	return 1;
L324:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doKw(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "do", 2) != 0) goto L325;
	if (idch((unsigned char)p[2])) goto L325;
	p += 2;
	actionLogPush(115, entry, p);	 
	return 1;
L325:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doWhileTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "while", 5) != 0) goto L326;
	if (idch((unsigned char)p[5])) goto L326;
	p += 5;
	actionLogPush(116, entry, p);	 
	return 1;
L326:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doCond(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L327;
	actionLogPush(117, entry, p);	 
	return 1;
L327:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L328;
	p += 1;
	return 1;
L328:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_doBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_statement()) goto L329;
	return 1;
L329:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_breakStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "break", 5) != 0) goto L330;
	if (idch((unsigned char)p[5])) goto L330;
	p += 5;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L330;
	p += 1;
	actionLogPush(120, entry, p);	 
	return 1;
L330:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_continueStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "continue", 8) != 0) goto L331;
	if (idch((unsigned char)p[8])) goto L331;
	p += 8;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L331;
	p += 1;
	actionLogPush(121, entry, p);	 
	return 1;
L331:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_gotoStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "goto", 4) != 0) goto L332;
	if (idch((unsigned char)p[4])) goto L332;
	p += 4;
	ws();
	if (!p_ident()) goto L332;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L332;
	p += 1;
	actionLogPush(122, entry, p);	 
	return 1;
L332:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_labelStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L333;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L333;
	p += 1;
	actionLogPush(123, entry, p);	 
	return 1;
L333:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_expr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L334;
	return 1;
L334:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_conditionalExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_orExpr()) goto L335;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_qmark()) goto L336;
	if (!p_conditionalTrue()) goto L336;
	if (!p_colon()) goto L336;
	if (!p_conditionalFalse()) goto L336;
	sp--; goto L337;
L336:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L337:	;
	actionLogPush(125, entry, p);	 
	return 1;
L335:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_parenOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L338;
	p += 1;
	actionLogPush(126, entry, p);	 
	return 1;
L338:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_parenClose(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L339;
	p += 1;
	actionLogPush(127, entry, p);	 
	return 1;
L339:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_commaExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_commaItem()) goto L340;
L341:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_commaTok()) goto L342;
	if (!p_commaItem()) goto L342;
	sp--; goto L341;
L342:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(128, entry, p);	 
	return 1;
L340:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_commaTok(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L343;
	p += 1;
	actionLogPush(129, entry, p);	 
	return 1;
L343:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_commaItem(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_commaAssign()) goto L346;
	goto L345;
L346:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_commaValue()) goto L347;
	goto L345;
L347:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L344;
L345:	sp--;
	return 1;
L344:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_commaAssign(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_target()) goto L348;
	if (!p_assignop()) goto L348;
	if (!p_expr()) goto L348;
	actionLogPush(131, entry, p);	 
	return 1;
L348:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_commaValue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L349;
	actionLogPush(132, entry, p);	 
	return 1;
L349:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_qmark(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "?", 1) != 0) goto L350;
	p += 1;
	actionLogPush(133, entry, p);	 
	return 1;
L350:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_conditionalTrue(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L351;
	return 1;
L351:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_colon(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, ":", 1) != 0) goto L352;
	p += 1;
	actionLogPush(135, entry, p);	 
	return 1;
L352:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_conditionalFalse(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_conditionalExpr()) goto L353;
	return 1;
L353:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_orExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_andExpr()) goto L354;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_orop()) goto L355;
	if (!p_orExpr()) goto L355;
	sp--; goto L356;
L355:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L356:	;
	actionLogPush(137, entry, p);	 
	return 1;
L354:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_orop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "||", 2) != 0) goto L357;
	p += 2;
	actionLogPush(138, entry, p);	 
	return 1;
L357:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_andExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitOrExpr()) goto L358;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_andop()) goto L359;
	if (!p_andExpr()) goto L359;
	sp--; goto L360;
L359:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L360:	;
	actionLogPush(139, entry, p);	 
	return 1;
L358:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_andop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&&", 2) != 0) goto L361;
	p += 2;
	actionLogPush(140, entry, p);	 
	return 1;
L361:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitOrExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitXorExpr()) goto L362;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitorop()) goto L363;
	if (!p_bitOrExpr()) goto L363;
	sp--; goto L364;
L363:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L364:	;
	actionLogPush(141, entry, p);	 
	return 1;
L362:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitorop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "|", 1) != 0) goto L365;
	if (strncmp(p, "|=", 2) == 0) goto L365;	 
	if (strncmp(p, "||", 2) == 0) goto L365;	 
	p += 1;
	actionLogPush(142, entry, p);	 
	return 1;
L365:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitXorExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_bitAndExpr()) goto L366;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitxorop()) goto L367;
	if (!p_bitXorExpr()) goto L367;
	sp--; goto L368;
L367:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L368:	;
	actionLogPush(143, entry, p);	 
	return 1;
L366:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitxorop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "^", 1) != 0) goto L369;
	if (strncmp(p, "^=", 2) == 0) goto L369;	 
	p += 1;
	actionLogPush(144, entry, p);	 
	return 1;
L369:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitAndExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_comparison()) goto L370;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_bitandop()) goto L371;
	if (!p_bitAndExpr()) goto L371;
	sp--; goto L372;
L371:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L372:	;
	actionLogPush(145, entry, p);	 
	return 1;
L370:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_bitandop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&", 1) != 0) goto L373;
	if (strncmp(p, "&=", 2) == 0) goto L373;	 
	if (strncmp(p, "&&", 2) == 0) goto L373;	 
	p += 1;
	actionLogPush(146, entry, p);	 
	return 1;
L373:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_comparison(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_shiftExpr()) goto L374;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_relop()) goto L375;
	if (!p_shiftExpr()) goto L375;
	sp--; goto L376;
L375:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L376:	;
	actionLogPush(147, entry, p);	 
	return 1;
L374:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_shiftExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L377;
L378:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_shiftop()) goto L379;
	if (!p_shiftRhs()) goto L379;
	sp--; goto L378;
L379:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L377:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_shiftRhs(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_addExpr()) goto L380;
	actionLogPush(149, entry, p);	 
	return 1;
L380:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_shiftop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "<<", 2) != 0) goto L383;
	if (strncmp(p, "<<=", 3) == 0) goto L383;	 
	p += 2;
	goto L382;
L383:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">>", 2) != 0) goto L384;
	if (strncmp(p, ">>=", 3) == 0) goto L384;	 
	p += 2;
	goto L382;
L384:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L381;
L382:	sp--;
	actionLogPush(150, entry, p);	 
	return 1;
L381:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_relop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "<=", 2) != 0) goto L387;
	p += 2;
	goto L386;
L387:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">=", 2) != 0) goto L388;
	p += 2;
	goto L386;
L388:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "==", 2) != 0) goto L389;
	p += 2;
	goto L386;
L389:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!=", 2) != 0) goto L390;
	p += 2;
	goto L386;
L390:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "<", 1) != 0) goto L391;
	if (strncmp(p, "<<=", 3) == 0) goto L391;	 
	if (strncmp(p, "<<", 2) == 0) goto L391;	 
	if (strncmp(p, "<=", 2) == 0) goto L391;	 
	p += 1;
	goto L386;
L391:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, ">", 1) != 0) goto L392;
	if (strncmp(p, ">>=", 3) == 0) goto L392;	 
	if (strncmp(p, ">>", 2) == 0) goto L392;	 
	if (strncmp(p, ">=", 2) == 0) goto L392;	 
	p += 1;
	goto L386;
L392:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L385;
L386:	sp--;
	actionLogPush(151, entry, p);	 
	return 1;
L385:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_addExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_term()) goto L393;
L394:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_addop()) goto L395;
	if (!p_term()) goto L395;
	sp--; goto L394;
L395:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L393:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_addop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "+", 1) != 0) goto L398;
	if (strncmp(p, "+=", 2) == 0) goto L398;	 
	if (strncmp(p, "++", 2) == 0) goto L398;	 
	p += 1;
	goto L397;
L398:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "-", 1) != 0) goto L399;
	if (strncmp(p, "-=", 2) == 0) goto L399;	 
	if (strncmp(p, "--", 2) == 0) goto L399;	 
	if (strncmp(p, "->", 2) == 0) goto L399;	 
	p += 1;
	goto L397;
L399:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L396;
L397:	sp--;
	actionLogPush(153, entry, p);	 
	return 1;
L396:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_term(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L400;
L401:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_mulop()) goto L402;
	if (!p_factor()) goto L402;
	sp--; goto L401;
L402:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(154, entry, p);	 
	return 1;
L400:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_mulop(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L405;
	if (strncmp(p, "*=", 2) == 0) goto L405;	 
	p += 1;
	goto L404;
L405:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "/", 1) != 0) goto L406;
	if (strncmp(p, "/=", 2) == 0) goto L406;	 
	p += 1;
	goto L404;
L406:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "%", 1) != 0) goto L407;
	if (strncmp(p, "%=", 2) == 0) goto L407;	 
	p += 1;
	goto L404;
L407:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L403;
L404:	sp--;
	actionLogPush(155, entry, p);	 
	return 1;
L403:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_factor(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_sizeofExpr()) goto L410;
	goto L409;
L410:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_castExpr()) goto L411;
	goto L409;
L411:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_preIncDec()) goto L412;
	goto L409;
L412:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L413;
	goto L409;
L413:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_parenOpen()) goto L414;
	if (!p_commaExpr()) goto L414;
	if (!p_parenClose()) goto L414;
	goto L409;
L414:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_call()) goto L415;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L416;
	sp--; goto L417;
L416:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L417:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_callMember()) goto L418;
	sp--; goto L419;
L418:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L419:	;
	goto L409;
L415:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectCall()) goto L420;
	goto L409;
L420:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_boolLit()) goto L421;
	goto L409;
L421:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_addressRef()) goto L422;
	goto L409;
L422:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_derefRef()) goto L423;
	goto L409;
L423:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_varRef()) goto L424;
	goto L409;
L424:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_charLit()) goto L425;
	goto L409;
L425:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_number()) goto L426;
	goto L409;
L426:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_stringLit()) goto L427;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_postfixIndex()) goto L428;
	sp--; goto L429;
L428:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L429:	;
	goto L409;
L427:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_negFactor()) goto L430;
	goto L409;
L430:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L408;
L409:	sp--;
	actionLogPush(156, entry, p);	 
	return 1;
L408:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_postfixIndex(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_index()) goto L431;
	actionLogPush(157, entry, p);	 
	return 1;
L431:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_stringLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\"", 1) != 0) goto L432;
	p += 1;
L433:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_character()) goto L434;
	sp--; goto L433;
L434:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	if (strncmp(p, "\"", 1) != 0) goto L432;
	p += 1;
	actionLogPush(158, entry, p);	 
	return 1;
L432:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_charLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "'", 1) != 0) goto L435;
	p += 1;
	if (!p_charLitBody()) goto L435;
	if (strncmp(p, "'", 1) != 0) goto L435;
	p += 1;
	actionLogPush(159, entry, p);	 
	return 1;
L435:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_charLitBody(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_charLitEscape()) goto L438;
	goto L437;
L438:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_charLitPlain()) goto L439;
	goto L437;
L439:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L436;
L437:	sp--;
	return 1;
L436:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_charLitEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L440;
	p += 1;
	if (!p_charLitEscChar()) goto L440;
	return 1;
L440:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_charLitEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "n", 1) != 0) goto L443;
	p += 1;
	goto L442;
L443:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "t", 1) != 0) goto L444;
	p += 1;
	goto L442;
L444:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "r", 1) != 0) goto L445;
	p += 1;
	goto L442;
L445:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "0", 1) != 0) goto L446;
	p += 1;
	goto L442;
L446:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "\\", 1) != 0) goto L447;
	p += 1;
	goto L442;
L447:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L441;
L442:	sp--;
	return 1;
L441:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_charLitPlain(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x26) goto L450;
	p++;
	goto L449;
L450:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x28 || (unsigned char)*p > 0x5B) goto L451;
	p++;
	goto L449;
L451:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x5D || (unsigned char)*p > 0x7E) goto L452;
	p++;
	goto L449;
L452:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L448;
L449:	sp--;
	return 1;
L448:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_character(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_strEscape()) goto L455;
	goto L454;
L455:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x21) goto L456;
	p++;
	goto L454;
L456:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x23 || (unsigned char)*p > 0x7E) goto L457;
	p++;
	goto L454;
L457:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L453;
L454:	sp--;
	return 1;
L453:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_strEscape(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "\\", 1) != 0) goto L458;
	p += 1;
	if (!p_strEscChar()) goto L458;
	return 1;
L458:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_strEscChar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) goto L459;
	p++;
	return 1;
L459:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_negFactor(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "-", 1) != 0) goto L462;
	if (strncmp(p, "-=", 2) == 0) goto L462;	 
	if (strncmp(p, "--", 2) == 0) goto L462;	 
	if (strncmp(p, "->", 2) == 0) goto L462;	 
	p += 1;
	goto L461;
L462:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "!", 1) != 0) goto L463;
	if (strncmp(p, "!=", 2) == 0) goto L463;	 
	p += 1;
	goto L461;
L463:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "~", 1) != 0) goto L464;
	p += 1;
	goto L461;
L464:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L460;
L461:	sp--;
	if (!p_factor()) goto L460;
	actionLogPush(167, entry, p);	 
	return 1;
L460:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_addressRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "&", 1) != 0) goto L465;
	if (strncmp(p, "&=", 2) == 0) goto L465;	 
	if (strncmp(p, "&&", 2) == 0) goto L465;	 
	p += 1;
	ws();
	if (!p_ident()) goto L465;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L466;
	sp--; goto L467;
L466:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L467:	;
	actionLogPush(168, entry, p);	 
	return 1;
L465:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_sizeofExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "sizeof", 6) != 0) goto L468;
	if (idch((unsigned char)p[6])) goto L468;
	p += 6;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L468;
	p += 1;
	if (!p_sizeofArg()) goto L468;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L468;
	p += 1;
	return 1;
L468:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_sizeofArg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_sizeofType()) goto L471;
	goto L470;
L471:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_sizeofVarName()) goto L472;
	goto L470;
L472:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L469;
L470:	sp--;
	return 1;
L469:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_sizeofType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_sizeofBaseType()) goto L473;
	if (!p_pointerDecl()) goto L473;
	actionLogPush(171, entry, p);	 
	return 1;
L473:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_sizeofBaseType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L476;
	if (idch((unsigned char)p[8])) goto L476;
	p += 8;
	if (!p_unsignedInt()) goto L476;
	goto L475;
L476:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L477;
	if (idch((unsigned char)p[3])) goto L477;
	p += 3;
	goto L475;
L477:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L478;
	if (idch((unsigned char)p[4])) goto L478;
	p += 4;
	goto L475;
L478:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L479;
	if (idch((unsigned char)p[4])) goto L479;
	p += 4;
	goto L475;
L479:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L480;
	if (idch((unsigned char)p[4])) goto L480;
	p += 4;
	goto L475;
L480:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L481;
	if (idch((unsigned char)p[6])) goto L481;
	p += 6;
	if (!p_structTypeRef()) goto L481;
	goto L475;
L481:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L482;
	if (idch((unsigned char)p[4])) goto L482;
	p += 4;
	if (!p_enumTypeRef()) goto L482;
	goto L475;
L482:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L483;
	if (idch((unsigned char)p[4])) goto L483;
	p += 4;
	goto L475;
L483:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L474;
L475:	sp--;
	return 1;
L474:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_sizeofVarName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L484;
	actionLogPush(173, entry, p);	 
	return 1;
L484:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_castExpr(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L485;
	p += 1;
	if (!p_castType()) goto L485;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L485;
	p += 1;
	if (!p_castOperand()) goto L485;
	actionLogPush(174, entry, p);	 
	return 1;
L485:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_castType(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_type()) goto L486;
	if (!p_pointerDecl()) goto L486;
	actionLogPush(175, entry, p);	 
	return 1;
L486:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_castOperand(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_factor()) goto L487;
	return 1;
L487:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_preIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_incdecOp()) goto L490;
	if (!p_derefIncTarget()) goto L490;
	goto L489;
L490:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L491;
	if (!p_memberIncTarget()) goto L491;
	goto L489;
L491:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L492;
	if (!p_indexIncTarget()) goto L492;
	goto L489;
L492:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_incdecOp()) goto L493;
	ws();
	if (!p_ident()) goto L493;
	goto L489;
L493:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L488;
L489:	sp--;
	actionLogPush(177, entry, p);	 
	return 1;
L488:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_postIncDec(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_derefIncTarget()) goto L496;
	if (!p_incdecOp()) goto L496;
	goto L495;
L496:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_memberIncTarget()) goto L497;
	if (!p_incdecOp()) goto L497;
	goto L495;
L497:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indexIncTarget()) goto L498;
	if (!p_incdecOp()) goto L498;
	goto L495;
L498:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (!p_ident()) goto L499;
	if (!p_incdecOp()) goto L499;
	goto L495;
L499:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L494;
L495:	sp--;
	actionLogPush(178, entry, p);	 
	return 1;
L494:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_memberIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L500;
	if (!p_member()) goto L500;
	return 1;
L500:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_indexIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L501;
	if (!p_index()) goto L501;
	return 1;
L501:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_derefIncTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L502;
	p += 1;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L502;
	if (strncmp(p, "*=", 2) == 0) goto L502;	 
	p += 1;
	ws();
	if (!p_ident()) goto L502;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L502;
	p += 1;
	return 1;
L502:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_incdecOp(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "++", 2) != 0) goto L505;
	p += 2;
	goto L504;
L505:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "--", 2) != 0) goto L506;
	p += 2;
	goto L504;
L506:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L503;
L504:	sp--;
	return 1;
L503:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_incDecStmt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_preIncDec()) goto L509;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L509;
	p += 1;
	goto L508;
L509:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_postIncDec()) goto L510;
	ws();
	if (strncmp(p, ";", 1) != 0) goto L510;
	p += 1;
	goto L508;
L510:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L507;
L508:	sp--;
	actionLogPush(183, entry, p);	 
	return 1;
L507:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_derefRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L511;
	if (strncmp(p, "*=", 2) == 0) goto L511;	 
	p += 1;
	if (!p_factor()) goto L511;
	actionLogPush(184, entry, p);	 
	return 1;
L511:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_call(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_funcName()) goto L512;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L512;
	p += 1;
	if (!p_argList()) goto L512;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L512;
	p += 1;
	actionLogPush(185, entry, p);	 
	return 1;
L512:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_callMember(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_member()) goto L513;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L514;
	sp--; goto L515;
L514:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L515:	;
	actionLogPush(186, entry, p);	 
	return 1;
L513:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_indirectCall(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_varRef()) goto L516;
	if (!p_indCallOpen()) goto L516;
	if (!p_argList()) goto L516;
	ws();
	if (strncmp(p, ")", 1) != 0) goto L516;
	p += 1;
	actionLogPush(187, entry, p);	 
	return 1;
L516:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_indCallOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "(", 1) != 0) goto L517;
	p += 1;
	actionLogPush(188, entry, p);	 
	return 1;
L517:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_argList(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_arg()) goto L519;
L521:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ",", 1) != 0) goto L522;
	p += 1;
	if (!p_arg()) goto L522;
	sp--; goto L521;
L522:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L520;
L519:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L520:	;
	return 1;
L518:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_arg(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_expr()) goto L523;
	actionLogPush(190, entry, p);	 
	return 1;
L523:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_target(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_directTarget()) goto L526;
	goto L525;
L526:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_indirectTarget()) goto L527;
	goto L525;
L527:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L524;
L525:	sp--;
	return 1;
L524:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_directTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L528;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L529;
L531:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L532;
	sp--; goto L531;
L532:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L530;
L529:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L530:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L533;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L535;
L537:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L538;
	sp--; goto L537;
L538:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L536;
L535:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L536:	;
	sp--; goto L534;
L533:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L534:	;
	actionLogPush(192, entry, p);	 
	return 1;
L528:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_indirectTarget(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L539;
	if (strncmp(p, "*=", 2) == 0) goto L539;	 
	p += 1;
	if (!p_factor()) goto L539;
	actionLogPush(193, entry, p);	 
	return 1;
L539:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_varRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L540;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L541;
L543:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L544;
	sp--; goto L543;
L544:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L542;
L541:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L542:	;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_member()) goto L545;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L547;
L549:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_index()) goto L550;
	sp--; goto L549;
L550:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	sp--; goto L548;
L547:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L548:	;
	sp--; goto L546;
L545:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
L546:	;
	actionLogPush(194, entry, p);	 
	return 1;
L540:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_index(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_indexOpen()) goto L551;
	if (!p_expr()) goto L551;
	ws();
	if (strncmp(p, "]", 1) != 0) goto L551;
	p += 1;
	actionLogPush(195, entry, p);	 
	return 1;
L551:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_indexOpen(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "[", 1) != 0) goto L552;
	p += 1;
	actionLogPush(196, entry, p);	 
	return 1;
L552:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_member(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, ".", 1) != 0) goto L555;
	if (strncmp(p, "...", 3) == 0) goto L555;	 
	p += 1;
	if (!p_fieldName()) goto L555;
	goto L554;
L555:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "->", 2) != 0) goto L556;
	p += 2;
	if (!p_fieldName()) goto L556;
	goto L554;
L556:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L553;
L554:	sp--;
	return 1;
L553:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_defName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L557;
	actionLogPush(198, entry, p);	 
	return 1;
L557:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_paramName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L558;
	return 1;
L558:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_localName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L559;
	actionLogPush(200, entry, p);	 
	return 1;
L559:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_globalName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L560;
	return 1;
L560:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_funcName(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L561;
	actionLogPush(202, entry, p);	 
	return 1;
L561:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_type(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "unsigned", 8) != 0) goto L564;
	if (idch((unsigned char)p[8])) goto L564;
	p += 8;
	if (!p_unsignedInt()) goto L564;
	goto L563;
L564:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "int", 3) != 0) goto L565;
	if (idch((unsigned char)p[3])) goto L565;
	p += 3;
	goto L563;
L565:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L566;
	if (idch((unsigned char)p[4])) goto L566;
	p += 4;
	goto L563;
L566:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L567;
	if (idch((unsigned char)p[4])) goto L567;
	p += 4;
	goto L563;
L567:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "bool", 4) != 0) goto L568;
	if (idch((unsigned char)p[4])) goto L568;
	p += 4;
	goto L563;
L568:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "struct", 6) != 0) goto L569;
	if (idch((unsigned char)p[6])) goto L569;
	p += 6;
	if (!p_structTypeRef()) goto L569;
	goto L563;
L569:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "enum", 4) != 0) goto L570;
	if (idch((unsigned char)p[4])) goto L570;
	p += 4;
	if (!p_enumTypeRef()) goto L570;
	goto L563;
L570:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "void", 4) != 0) goto L571;
	if (idch((unsigned char)p[4])) goto L571;
	p += 4;
	goto L563;
L571:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_typedefRef()) goto L572;
	goto L563;
L572:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L562;
L563:	sp--;
	actionLogPush(203, entry, p);	 
	return 1;
L562:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_structTypeRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L573;
	return 1;
L573:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_enumTypeRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L574;
	return 1;
L574:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_typedefRef(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (!p_ident()) goto L575;
	return 1;
L575:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_pointerDecl(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
L577:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_pointerStar()) goto L578;
	sp--; goto L577;
L578:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	actionLogPush(207, entry, p);	 
	return 1;
L576:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_pointerStar(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	ws();
	if (strncmp(p, "*", 1) != 0) goto L579;
	if (strncmp(p, "*=", 2) == 0) goto L579;	 
	p += 1;
	return 1;
L579:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_unsignedInt(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "int", 3) != 0) goto L582;
	if (idch((unsigned char)p[3])) goto L582;
	p += 3;
	goto L581;
L582:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "char", 4) != 0) goto L583;
	if (idch((unsigned char)p[4])) goto L583;
	p += 4;
	goto L581;
L583:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "long", 4) != 0) goto L584;
	if (idch((unsigned char)p[4])) goto L584;
	p += 4;
	goto L581;
L584:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L580;
L581:	sp--;
	return 1;
L580:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_boolLit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	ws();
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	ws();
	if (strncmp(p, "true", 4) != 0) goto L587;
	if (idch((unsigned char)p[4])) goto L587;
	p += 4;
	goto L586;
L587:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	ws();
	if (strncmp(p, "false", 5) != 0) goto L588;
	if (idch((unsigned char)p[5])) goto L588;
	p += 5;
	goto L586;
L588:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L585;
L586:	sp--;
	actionLogPush(210, entry, p);	 
	return 1;
L585:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_ident(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_letter()) goto L589;
L590:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_letter()) goto L593;
	goto L592;
L593:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_digit()) goto L594;
	goto L592;
L594:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L591;
L592:	sp--;
	sp--; goto L590;
L591:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L589:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_number(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexNumber()) goto L597;
	goto L596;
L597:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (!p_decNumber()) goto L598;
	goto L596;
L598:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L595;
L596:	sp--;
	actionLogPush(212, entry, p);	 
	return 1;
L595:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_hexNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (strncmp(p, "0", 1) != 0) goto L599;
	p += 1;
	if (!p_hexMark()) goto L599;
	if (!p_hexDigit()) goto L599;
L600:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_hexDigit()) goto L601;
	sp--; goto L600;
L601:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L599:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_hexMark(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (strncmp(p, "x", 1) != 0) goto L604;
	p += 1;
	goto L603;
L604:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "X", 1) != 0) goto L605;
	p += 1;
	goto L603;
L605:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L602;
L603:	sp--;
	return 1;
L602:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_hexDigit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L608;
	goto L607;
L608:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x66) goto L609;
	p++;
	goto L607;
L609:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x46) goto L610;
	p++;
	goto L607;
L610:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L606;
L607:	sp--;
	return 1;
L606:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_decNumber(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if (!p_digit()) goto L611;
L612:	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if (!p_digit()) goto L613;
	sp--; goto L612;
L613:	sp--; p = sv[sp]; actionLogLen = svLog[sp];
	return 1;
L611:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_letter(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	sv[sp] = p; svLog[sp] = actionLogLen; sp++;
	if ((unsigned char)*p < 0x61 || (unsigned char)*p > 0x7A) goto L616;
	p++;
	goto L615;
L616:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if ((unsigned char)*p < 0x41 || (unsigned char)*p > 0x5A) goto L617;
	p++;
	goto L615;
L617:	p = sv[sp-1]; actionLogLen = svLog[sp-1];
	if (strncmp(p, "_", 1) != 0) goto L618;
	p += 1;
	goto L615;
L618:	sp--; p = sv[sp]; actionLogLen = svLog[sp]; goto L614;
L615:	sp--;
	return 1;
L614:	p = entry; actionLogLen = entryLog;
	return 0;
}

 
static int p_digit(void) {
	const char* sv[64]; int svLog[64]; int sp;
	const char* entry; int entryLog;
	sp = 0; entry = p; entryLog = actionLogLen;
	(void)sv; (void)svLog; (void)sp; (void)entryLog;
	if ((unsigned char)*p < 0x30 || (unsigned char)*p > 0x39) goto L619;
	p++;
	return 1;
L619:	p = entry; actionLogLen = entryLog;
	return 0;
}

static char* inputFileBuf;

int main(int argc, char** argv) {
	FILE* inputFile; size_t inputLen;
	if (argc < 2) { fprintf(stderr, "usage: %s <eingabe>\n", argv[0]); return 2; }
	if (*argv[1] == '@') {
		inputFile = fopen(argv[1] + 1, "r");
		if (!inputFile) { fprintf(stderr, "can't open %s\n", argv[1] + 1); return 2; }
		inputFileBuf = realloc(0, 524288);
		if (!inputFileBuf) { fclose(inputFile); fprintf(stderr, "out of memory\n"); return 2; }
		inputLen = fread(inputFileBuf, 1, 524288 - 1, inputFile);
		fclose(inputFile); inputFileBuf[inputLen] = '\0'; p = inputFileBuf;
	} else p = argv[1];
	parserInputStart = p;
	if (p_program()) { ws(); if (*p == '\0') { actionLogReplay(); if (actionErrors != 0) { printf("SEMERR\n"); (void)0; return 1; } printf("OK\n"); (void)0; return 0; } }
	printf("FAIL\n"); (void)0;
	return 1;
}
