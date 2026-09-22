Quant9 — Generics (Entwurf, monomorphisation-only)
===============================================

Kurzfassung
-----------
Wir unterstützen zunächst parametrische Container (Array<K>, Map<K,V>) mittels
Monomorphisation: für jede tatsächlich verwendete Type-Instanz erzeugt der
Compiler eine konkrete Typ-Instanz (Array_int32, Map_string_int32). Das hält
Codegen einfach und performant (keine Boxing/erasure).

Syntax (klein & präzise)
-----------------------
- Generic type usage: Identifier "<" TypeList ">"  (z.B. Array<int32>, Map<string,int32>)
- Array literal syntax T[n] bleibt für fixed-size arrays optional (legacy)

EBNF (Ergänzung, kurz)
----------------------
SimpleType = Identifier [ "<" TypeList ">" ] .
TypeList   = Type { "," Type } .
Type       = SimpleType | PointerType | ArrayType .
PointerType= "*" Type .
ArrayType  = Type "[" Number "]" .

Beispiel-Quellcode (Quant9)
---------------------------
# Deklariere und benutze ein typisiertes Array
function example_array() : int {
    let a : Array<int32> = new Array<int32>(10);  # allocate 10 elements
    let i : int32 = 3;
    a[i] = 42;
    let x : int32 = a[i];
    return x;
}

Monomorphisation — Ablauf (Kurz)
-------------------------------
1. Parser/AST: erkennt SimpleType "Array<int32>" und erstellt eine TypeUse node.
2. TypeChecker: beim ersten Vorkommen von Array<T> mit konkretem T erzeugt
   eine ConcreteType entry: Array_int32 with element_size = sizeof(int32).
3. Lowering: alle Operationen auf Array<int32> (alloc, index, load, store)
   werden in IR auf die konkrete ConcreteType gemappt (keine boxing).
4. Backend: arbeitet gegen ConcreteType-Layouts; keine Generic-Runtime nötig.

Lowering-Beispiel (zu QCC Stack IR)
----------------------------------
Quellzeile: a[i] = 42;
Annahmen: lokale pointer slot for `a` at index P_SLOT, local int slots for i (LIDX)

QIR (schematisch):
  ; compute address p' = ptr + i * sizeof(elem)
  LOADP P_SLOT         ; -> p
  LOADL LIDX           ; -> idx
  PUSH <element_size>  ; -> size
  MUL                  ; idx * size
  PADD i  (uses typetag for element)   ; p, idx -> p'
  PUSH 42
  STOREIND <typetag>   ; write value through pointer

(Lowering note: PADD / IPADD vs PTRINDEX may be used depending on exact IR opcodes.
 On our QIR: PTRINDEX <typetag> / LOADIND/STOREIND are available and preferred.)

Array allocation (new Array<int32>(n))
--------------------------------------
Lower to runtime allocator call + store pointer:
  PUSH n
  PUSH <element_size>
  CALL AllocArray 2    ; runtime returns pointer p to block (zeroed or uninit per policy)
  STOREP P_SLOT

Map<K,V> sketch
---------------
- Map literal/creation lowered to runtime map_create(elem_key_size, elem_value_size)
- Iteration (for k,v in map) lowered to map_iterator_init; loop over buckets via calls to
  runtime iterator functions (map_iter_next -> returns (hasNext, keyPtr, valPtr)).
- Concrete Map instantiation: Map_string_int32 has concrete key/value layout and helper
  functions (map_get_string_int32, map_put_string_int32).

Design‑entscheidungen (kurz)
---------------------------
- Monomorphisation: einfache Codegen, no boxing; recommended default.
- Cross‑module: instantiate per compilation unit; linker may dedupe identical instantiations
  by name-mangling scheme (later phase).
- No generic methods in Phase 1; only container types.

Nächste Schritte (technisch)
---------------------------
- Implement AST node support for SimpleType with type arguments.
- Extend typechecker to register ConcreteType entries on first use.
- Lower Array/Map ops to concrete-type IR sequences (example above).
- Add tests: Array<int32> indexing, Array<*Controller> with pointer elem, Map<string,int32> put/get.


