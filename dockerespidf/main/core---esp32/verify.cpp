#include "pxt.h"

namespace pxt {

VMImage *setVMImgError(VMImage *img, int code, void *pos) {
    img->errorOffset = pos ? (int)((uint8_t *)pos - (uint8_t *)img->dataStart) : 0;
    img->errorCode = code;
    return img;
}

// next free error 1066
#define ERROR(code, pos) return setVMImgError(img, code, pos)
#define CHECK(cond, code)                                                                          \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ERROR(code, sect);                                                                     \
    } while (0)
#define CHECK_AT(cond, code, pos)                                                                  \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ERROR(code, pos);                                                                      \
    } while (0)
#define ALIGNED(sz) (((sz)&7) == 0)

#define FOR_SECTIONS()                                                                             \
    VMImageSection *sect, *next;                                                                   \
    for (sect = (VMImageSection *)img->dataStart;                                                  \
         (next = vmNextSection(sect), (uint64_t *)sect < img->dataEnd); sect = next)

// RIC DEBUG: acumulado de bytes reservados en RAM durante la carga/parcheo
// de la imagen (countSections/loadSections/loadIfaceNames). Es memoria
// "fija" (no gestionada por el GC) que vive mientras dure vmImg: tablas de
// punteros a secciones, opcodes, literales numericos, nombres de interfaz...
// Se resetea al principio de cada loadVMImage() para poder ver, imagen a
// imagen, cuanta RAM extra necesita el loader aparte del heap del GC.
static uint32_t g_imgLoadBytes = 0;

void *xmallocNamed(const char *label, uint32_t count, uint32_t elemSize)
{
	uint32_t bytes = (count == 0) ? 1 : elemSize * count;
	g_imgLoadBytes += bytes;

	DMESG("    [MEM-IMG] %s: %d elementos x %d bytes = %d bytes (acumulado carga = %d bytes)",label, count, elemSize, bytes, g_imgLoadBytes);

	return xmalloc(bytes);
}

#define VM_MAX_PATCH 7

struct VMPatchState {
    uint32_t offset;
    uint32_t bytesLeftInSect;
    uint8_t patchOff;
    const char *error;
    uint64_t patch[VM_MAX_PATCH];
};

// RIC
static const VTable *vtFor(VMImageSection *sect)
{
	
	// RIC
	//DMESG("vtFor(...) for type = %d",sect->type);
	
	if (sect->type == SectionType::Function)
		{
        return &pxt::RefAction_vtable;
		}
    else if (sect->type == SectionType::Literal)
		{
        
		switch ((BuiltInType)sect->aux)
			{
			case BuiltInType::BoxedString_ASCII:
				return &pxt::string_inline_ascii_vt;
			case BuiltInType::BoxedString_SkipList:
				return &pxt::string_skiplist16_packed_vt;
			case BuiltInType::BoxedString:
				return &pxt::string_inline_utf8_vt;
			case BuiltInType::BoxedBuffer:
				return &pxt::buffer_vt;
			default:
				// RIC	
				DMESG("vtFor(...) error");
				return NULL;
			}
		}
	
	// RIC	
	//DMESG("vtFor(...) for type %d return NULL",sect->type);
	
	return NULL;
}

static bool isStringSection(VMImageSection *sect) {
    if (sect->type == SectionType::Literal)
        switch ((BuiltInType)sect->aux) {
        case BuiltInType::BoxedString_ASCII:
        case BuiltInType::BoxedString_SkipList:
        case BuiltInType::BoxedString:
            return true;
        default:
            return false;
        }
    return false;
}

VMPatchState *vm_alloc_patch_state() {
    return (VMPatchState *)calloc(sizeof(VMPatchState), 1);
}

void vm_finish_patch(VMPatchState *state) {
    free(state);
}

// RIC
const char *vm_patch_image(VMPatchState *state, uint8_t *data, uint32_t len)
{
	DMESG("vm_patch_image( ... , data = %x , len = %d )",data,len);
	
	if (state->error)					return state->error;
    if (len <= 8 || !ALIGNED(len))		return (state->error = "invalid chunk size");

    while (len > 0)
		{
			
		if (state->bytesLeftInSect == 0)					// inicio de una nueva seccion
			{
			
			VMImageSection sect;
			memcpy(&sect, data, sizeof(sect));

			if (!ALIGNED(sect.size) || !sect.size)
				{
				DMESG("ERROR vm_patch_image(...) -> invalid section");
				return (state->error = "invalid section");
				}
			
			state->bytesLeftInSect = sect.size;
			//DMESG("    new section de size = %d bytes",state->bytesLeftInSect);

			memset(state->patch, 0, sizeof(state->patch));
			state->patchOff = 1;

			const VTable *vt = NULL;
			
			if (sect.type == SectionType::Literal || sect.type == SectionType::Function)		// Literal o Fucntion
				{
				
				vt = vtFor(&sect);
			
				if (!vt) return (state->error = "unknown literal or function vt");
				
				// RIC antes -> uint64_t vt_val=( (uint64_t)(uint32_t)vt ) << 32;
				uint64_t vt_val=( (uint64_t)(uintptr_t)vt ) << 32;
				
				// RIC DEBUG - mostramos info del parcheo que hace
				{
				uint64_t original_raw;
				memcpy(&original_raw, data + 8, sizeof(original_raw));   		// +8: saltamos la cabecera de 8 bytes, esto ya es el body (vt-ref)
				uint32_t orig_hi = (uint32_t)(original_raw >> 32);
				uint32_t orig_lo = (uint32_t)(original_raw & 0xFFFFFFFF);
				if (sect.type == SectionType::Literal) 
					{
					if ( vt == &pxt::string_inline_ascii_vt )
						{
						//DMESG("vm_patch_image(...) -> Literal section (%x,%x) <- string_inline_ascii_vt",orig_hi,orig_lo);
						}
					if ( vt == &pxt::string_skiplist16_packed_vt )
						{
						DMESG("vm_patch_image(...) -> Literal section (%x,%x) <- string_skiplist16_packed_vt",orig_hi,orig_lo);
						}
					if ( vt == &pxt::string_inline_utf8_vt )
						{
						DMESG("vm_patch_image(...) -> Literal section (%x,%x) <- string_inline_utf8_vt",orig_hi,orig_lo);
						}
					if ( vt == &pxt::buffer_vt )
						{
						//DMESG("vm_patch_image(...) -> Literal section (%x,%x) <- buffer_vt",orig_hi,orig_lo);
						}
					}
				if ( sect.type == SectionType::Function && vt == &pxt::RefAction_vtable )
					{
					//DMESG("vm_patch_image(...) -> Function section (%x,%x) <- RefAction_vtable",orig_hi,orig_lo);
					}
				}

				#ifdef PXT64
					state->patch[0] = (uint64_t)vt;
				#else
					state->patch[0] = vt_val;
				#endif
				
				}
			else if (sect.type == SectionType::VTable)											// VTable
				{
				/*	VTables , en el formato PXT, la cabecera de la sección VTable contiene información sobre
					la clase, seguida inmediatamente de una región de 32 bytes
					( VM_NUM_CPP_METHODS = 4 métodos * 8 bytes (por puntero en 64 bits) )
					para métodos nativos. 
					Cabecera de la VTable: ocupa los primeros 16 bytes del cuerpo.
					methods[4*8]: ocupa los siguientes 32 bytes (los métodos C++ nativos del runtime).
					En el fichero binario .pxt64 estos 32 bytes vienen rellenos a cero porque las
					direcciones de memoria reales de las funciones C++ dependen de dónde se haya
					compilado/cargado el runtime.  
				*/
				
				auto dest = (void **)((uint32_t *)state->patch + 4);			// saltamos al inicio de -> vt->methods[4*8];
				dest[0] = (void *)pxt::RefRecord_destroy;
				dest[1] = (void *)pxt::RefRecord_print;
				dest[2] = (void *)pxt::RefRecord_scan;
				dest[3] = (void *)pxt::RefRecord_gcsize;
				}
			else if (sect.type == SectionType::NumberBoxes)										// NumberBoxes
				{
					
			#ifdef PXT32
				/*	antes habia esto y parada el parcheo y NO acababa ...
				
				if (sect.type == SectionType::NumberBoxes)
					{				
					return (state->error = "TODO: NumberBoxes");
					}
					
				*/
				
				// Layout real: entradas de 12 bytes (vtable:4 + num:8), sin sentinel.
				// No encaja en el patch genérico de 8 en 8 bytes -> lo hacemos aparte,
				// tocando solo los 4 bytes del vtable de cada entrada.
				uint8_t *body    = data + sizeof(VMImageSection);
				uint32_t bodyLen = sect.size - sizeof(VMImageSection);
				uint32_t numEntries = bodyLen / 12;
				
				uint32_t vtBits = (uint32_t)(uintptr_t)&pxt::number_vt;
				for (uint32_t i = 0; i < numEntries; ++i)
					{
					// solo el campo vtable, el double no se toca
					memcpy(body + (size_t)i * 12, &vtBits, 4);
					}
				
				// ya está todo parcheado; que el bucle principal se limite a "consumir"
				// los bytes de esta sección sin volver a tocarlos
				state->patchOff = 0;
				
			#endif
				
				}
			
			}
		else if (state->patchOff != 0)						// vamos consumiendo los bytes de la seccion restantes y aplicamos patch
			{
			
			uint64_t p = state->patch[state->patchOff - 1];
			
			// aqui aplica el parche
			if (p)
				{
				//DMESG("        aplicado patch a esta seccion");
				memcpy(data, &p, sizeof(p));
				}
			
			if (state->patchOff == VM_MAX_PATCH)
				{
				state->patchOff = 0;
				}
			else
				{
				state->patchOff++;
				}
			
			}

		state->bytesLeftInSect -= 8;
		data += 8;
		len -= 8;
		
		}

    return NULL;
}

static VMImage *countSections(VMImage *img)
{
	
	auto p = img->dataStart;
	
    while (p < img->dataEnd)
		{
        auto sect = (VMImageSection *)p;
        CHECK(ALIGNED(sect->size), 1002);
        CHECK(sect->size > 0, 1002);
        img->numSections++;
        p += sect->size >> 3;
		}

	// RIC
	DMESG("    img->numSections = %d",img->numSections);
		
    CHECK_AT(p == img->dataEnd, 1003, p);
    
	img->pointerLiterals = ALLOC_ARRAY(TValue, img->numSections, "pointerLiterals");
    
	img->sections = ALLOC_ARRAY(VMImageSection *, img->numSections, "sections");
	
    return NULL;
}

// RIC
static VMImage *loadSections(VMImage *img)
{
	
	auto idx = 0;
    VMImageSection *numberBoxes = NULL;

    FOR_SECTIONS()
		{
        
		CHECK(sect->size < 32000, 1014);
        CHECK(sect->size >= 16, 1048);

        if (sect->type == SectionType::InfoHeader)
			{
            
			CHECK(sect->size >= sizeof(VMImageHeader), 1008);
            auto hd = (VMImageHeader *)sect->data;
            CHECK(hd->magic0 == VM_MAGIC0, 1009);
            CHECK(hd->magic1 == VM_MAGIC1, 1010);
            CHECK(hd->allocGlobals >= hd->nonPointerGlobals, 1011);
            CHECK(hd->allocGlobals < 10000, 1012);
            CHECK(idx == 0, 1013);
            img->infoHeader = hd;
			
			DMESG("    SectionType::InfoHeader loaded OK");
			
			}

        if (sect->type == SectionType::OpCodeMap)
			{
			
			CHECK(img->opcodes == NULL, 1015);
			
			auto curr = sect->data;
			auto endp = sect->data + sect->size - 8;
			CHECK(endp[-1] == 0, 1017);

			while (curr < endp)
				{
				if (*curr == 0) img->numOpcodes++;
				curr++;
				}
			CHECK(img->numOpcodes >= VM_FIRST_RTCALL, 1016);

			img->opcodes = ALLOC_ARRAY(OpFun, img->numOpcodes, "opcodes");
			img->opcodeDescs = ALLOC_ARRAY(const OpcodeDesc *, img->numOpcodes, "opcodeDescs");
			#ifdef VM_COST_ENABLED
				img->opcodeCostNs = ALLOC_ARRAY(uint32_t, img->numOpcodes, "opcodeCostNs");
			#endif
			
			int i = 0;
			curr = sect->data;
			while (curr < endp)
				{
				
				img->opcodeDescs[i] = NULL;
				img->opcodes[i] = NULL;
				#ifdef VM_COST_ENABLED
					img->opcodeCostNs[i] = 0;
				#endif
				
				if (*curr)
					{
					
					// buscamos en la tabla de funciones del runtime por nombre, esta definido en pointers.cpp
					for (auto st = staticOpcodes; st->name; st++)
						{
						
						if (strcmp(st->name, (const char *)curr) == 0)
							{
							img->opcodeDescs[i] = st;
							break;
							}
							
						}
					
					if (img->opcodeDescs[i] == NULL)
						{
						DMESG("missing opcode: %s", (const char *)curr);
						
						setVMImgError(img, 1018, curr);
						}
					else
						{
						//DMESG("found opcode: %s", (const char *)curr);
						
						img->opcodes[i] = img->opcodeDescs[i]->fn;
						
						#ifdef VM_COST_ENABLED
							// coste simulado por NOMBRE de opcode (ver vmcost.h), resuelto una sola vez al cargar la imagen
							img->opcodeCostNs[i] = vmcost_lookup(img->opcodeDescs[i]->name, i < VM_FIRST_RTCALL);
						#endif
						}
						
					}
				
				while (*curr) curr++;
				
				curr++;
				i++;
				}
			
			if (img->errorCode) return img;
				
			DMESG("    SectionType::OpCodeMap loaded OK");
			
			}

        if (sect->type == SectionType::NumberBoxes)
			{
            CHECK(!numberBoxes, 1061);
            numberBoxes = sect;
			
			DMESG("    SectionType::NumberBoxes loaded OK");
			}

        if (sect->type == SectionType::NumberLiterals)
			{				
			
			CHECK(!!numberBoxes, 1062);
			CHECK(!img->numberLiterals, 1004);
			
			img->numNumberLiterals = (sect->size >> 3) - 1;
			uint64_t *values = (uint64_t *)sect->data;

			int numBoxed = 0;

			for (unsigned i = 0; i < img->numNumberLiterals; ++i)
				{
				
				auto ptr = &values[i];
				uint64_t v = *ptr;
				
				if (isEncodedDouble(v))
					{
					CHECK_AT(!isnan(decodeDouble(v)), 1005, ptr);
					numBoxed++;
					}
				else if (v & 1)
					{
					CHECK_AT((v >> 1) <= 0xffffffff, 1006, ptr);
					if (!canBeTagged(v >> 1)) numBoxed++;
					}
				else if (v == 0)
					{
					// OK - padding probably
					}
				else
					{
					CHECK_AT(false, 1007, ptr);
					}
					
				}

			CHECK(numberBoxes->size >= sizeof(VMImageSection) + (numBoxed + 1) * 12, 1063);
			// RIC PATCH
			// antes habia esto pero petaba -> CHECK(numberBoxes->size <= 4 + sizeof(VMImageSection) + (numBoxed + 1) * 12, 10631);
			if ( !( numberBoxes->size <= ( 4 + sizeof(VMImageSection) + (numBoxed + 1) * 12 ) ) )
				{
				DMESG("ERROR patch numberBoxes->size = %d <= %d",numberBoxes->size,( 4 + sizeof(VMImageSection) + (numBoxed + 1) * 12 ));
				}
			else
				{
				DMESG("    numberBoxes->size = %d <= %d",numberBoxes->size,( 4 + sizeof(VMImageSection) + (numBoxed + 1) * 12 ));					
				}
			
			img->numberLiterals = ALLOC_ARRAY(TValue, img->numNumberLiterals, "numberLiterals");

			#ifdef PXT32
				img->boxedNumbers = (BoxedNumber *)numberBoxes->data;
				int boxedPtr = 0;
			#endif

			for (unsigned i = 0; i < img->numNumberLiterals; ++i)
				{
				
				uint64_t v = values[i];
				
				#ifdef PXT32
				
					if (!isEncodedDouble(v) && canBeTagged(v >> 1))
						{
						img->numberLiterals[i] = (TValue)v;
						}
					else
						{
						CHECK(boxedPtr < numBoxed, 1060); // should never happen
						double x = isEncodedDouble(v) ? decodeDouble(v) : (int32_t)(v >> 1);
						CHECK(img->boxedNumbers[boxedPtr].vtable == &number_vt, 1064);
						CHECK(img->boxedNumbers[boxedPtr].num == x, 1065);
						img->numberLiterals[i] = (TValue)&img->boxedNumbers[boxedPtr];
						boxedPtr++;
						}
						
				#else
					
					img->numberLiterals[i] = (TValue)v;
					
				#endif
				
				}
			
			DMESG("    SectionType::NumberLiterals loaded OK");		
			}

        if (sect->type == SectionType::ConfigData)
			{
			
			img->numConfigDataEntries = (sect->size - 8) >> 3;
            img->configData = (int32_t *)sect->data;
            CHECK(img->configData[(img->numConfigDataEntries - 1) * 2] == 0, 1045);
			
			DMESG("    SectionType::ConfigData loaded OK");
			}

		// como ya ha pasado validaciones anteriores, podemos asignarla que es SEGURA (no datos corruptos)
        img->sections[idx] = sect;

        if (sect->type == SectionType::Literal)
			{
				
            CHECK(sect->size >= 20, 1066);
			
            switch ((BuiltInType)sect->aux)
				{
				
				case BuiltInType::BoxedString_ASCII:				// string normal de C 1 char <-> 1 byte, el ultimo es 0
				case BuiltInType::BoxedString:						// String codificado en UTF8, son bytes seguidos, el utlimo es 0
					{
					auto p = (BoxedString *)vmLiteralVal(sect);
					CHECK(sect->size >= 16 + 2 + (uint32_t)p->ascii.length + 1, 1067);
					CHECK(p->ascii.data[p->ascii.length] == 0, 1068);
					break;
					}
					
				// es un tipo UTF8 de optimización avanzada para cadenas UTF-8 largas, tiene accesos a ciertos puntos de la cadena, ya que buscar un caracter concreto no es acceso lineal
				case BuiltInType::BoxedString_SkipList:
					{
					
					auto p = (BoxedString *)vmLiteralVal(sect);
					CHECK(sect->size >= 16 + 4 + PXT_NUM_SKIP_ENTRIES(p) * 2 + (uint32_t)p->skip_pack.size + 1,1069);
					CHECK(PXT_SKIP_DATA_PACK(p)[p->skip_pack.size] == 0, 1070);
					
					for (int i = 0; i < PXT_NUM_SKIP_ENTRIES(p); ++i)
						{
						CHECK(p->skip_pack.list[i] <= p->skip_pack.size, 1071);
						}
					
					DMESG("Tenemos un BuiltInType::BoxedString_SkipList");
					
					break;
					}
				
				// buffer de bytes , que tienen cualquier tipo de informacion
				case BuiltInType::BoxedBuffer:
					{
					auto p = (BoxedBuffer *)vmLiteralVal(sect);
					CHECK(sect->size >= 16 + 4 + (uint32_t)p->length, 1072);
					break;
					}
					
				default:
					CHECK(false, 1050);
					
				}
			
            img->pointerLiterals[idx] = vmLiteralVal(sect);
            // TODO validate size/length of boxed string/buffer; check utf8 encoding?; 1042 error
			
			//DMESG("    SectionType::Literal loaded OK");
			}
		else if (sect->type == SectionType::Function)
			{
			img->pointerLiterals[idx] = vmLiteralVal(sect);
            
			if (!img->entryPoint) img->entryPoint = (RefAction *)img->pointerLiterals[idx];
			
			//DMESG("    SectionType::Function loaded OK");
			}
		else if (sect->type == SectionType::VTable)
			{
            img->pointerLiterals[idx] = (TValue)(sect->data);
			
			//DMESG("    SectionType::VTable loaded OK");
			}
		else
			{
            img->pointerLiterals[idx] = nullptr;
			
			DMESG("        img->pointerLiterals la seccion %d queda a NULL",idx);
			}

        idx++;
		
		}

    CHECK_AT(img->infoHeader != NULL, 1019, 0);
    CHECK_AT(img->opcodes != NULL, 1020, 0);
    CHECK_AT(img->numberLiterals != NULL, 1021, 0);
    CHECK_AT(img->configData != NULL, 1022, 0);
    CHECK_AT(img->entryPoint != NULL, 1059, 0);

    return NULL;
}

static VMImage *loadIfaceNames(VMImage *img) {
    FOR_SECTIONS() {
        if (sect->type == SectionType::IfaceMemberNames) {
            uint32_t *ptrs = (uint32_t *)sect->data;
            auto len = *ptrs++;
            CHECK(len < 0x40000, 1047);
			uintptr_t *dst = ALLOC_ARRAY(uintptr_t, len + 1, "ifaceMemberNames");
            img->ifaceMemberNames = dst;
            img->numIfaceMemberNames = len;
            *dst++ = len;
            CHECK(sect->size >= 12 + len * 4, 1047);
            for (unsigned i = 0; i < len; ++i) {
                CHECK(ptrs[i] < img->numSections, 1051);
                auto ss = img->sections[ptrs[i]];
                CHECK(isStringSection(ss), 1052);
                dst[i] = (uintptr_t)img->pointerLiterals[ptrs[i]];
                // pointers have to be sorted
                CHECK(i == 0 || dst[i - 1] < dst[i], 1053);
                // and so strings
                CHECK(i == 0 || String_::compare((String)dst[i - 1], (String)dst[i]) < 0, 1054);
            }
        }
    }

    return NULL;
}

void validateFunction(VMImage *img, VMImageSection *sect, int debug);

static VMImage *validateFunctions(VMImage *img)
{
	
	int numseccion=0;
	
	FOR_SECTIONS()
		{
			
		// RIC DEBUG
		VALIDATE_FUNCTIONS_DBG("Section %d start",numseccion);
		
        if (sect->type == SectionType::VTable)
			{
			
			uint8_t *endp = sect->data + sect->size - 8;
			auto vt = (VTable *)sect->data;
			auto multBase = (uint16_t *)&vt->methods[VM_NUM_CPP_METHODS];
			CHECK((uint8_t *)multBase < endp,1023); // basic size check, before dereferencing anything

			auto maxMult = 0xffffffffU >> (vt->ifaceHashMult & 0xff);

			CHECK(vt->numbytes < 1024, 1024);
			CHECK((vt->numbytes & 7) == 0, 1025);
			CHECK(vt->objectType == ValType::Object, 1026);
			CHECK(vt->magic == VTABLE_MAGIC, 1027);
			CHECK(vt->ifaceHashEntries > maxMult + 3, 1028);
			CHECK((uint8_t *)(multBase + vt->ifaceHashEntries) < endp, 1029);
			CHECK(vt->reserved == 0, 1030);
			CHECK(vt->ifaceHashMult != 0, 1031);
			CHECK((vt->ifaceHashEntries & 3) == 0, 1032);
			CHECK((int)vt->classNo >= (int)BuiltInType::User0, 1055);
			CHECK((int)vt->lastClassNo >= (int)vt->classNo, 1056);

			uint32_t maxOff = 0;
			uint32_t minOff = 0xfffffff;
			for (unsigned i = 0; i < vt->ifaceHashEntries; ++i)
				{
				
				uint32_t off2 = multBase[i];
				
				if (off2 > maxOff) maxOff = off2;
				if (off2 < minOff) minOff = off2;
				
				auto ent = (IfaceEntry *)multBase + off2;
				
				CHECK((uint8_t *)(ent + 1) <= endp, 1033);
				
				}

			CHECK(minOff * sizeof(IfaceEntry) == vt->ifaceHashEntries * 2, 1034);

			auto last1 = (IfaceEntry *)multBase + maxOff + 1;
			
			if (last1->memberId != 0) maxOff++;

			for (unsigned i = minOff; i <= maxOff; ++i)
				{
				
				auto ent = (IfaceEntry *)multBase + i;
				
				if (ent->memberId == 0) continue;
				
				if (ent->aux == 0)
					{
					CHECK(ent->method < (unsigned)(vt->numbytes >> 3), 1035);
					}
				else
					{
					CHECK(ent->method < img->numSections, 1037);
					auto fn = img->sections[ent->method];
					CHECK(fn->type == SectionType::Function, 1039);
					}
				
				}

			auto p = (uint8_t *)((IfaceEntry *)multBase + maxOff + 1);
			
			while (p < endp) CHECK(*p++ == 0, 1040);
		
			}

        if (sect->type == SectionType::Function)
			{
            
			#ifdef VALIDATE_FUNCTIONS
				// activamos DEBUG
				validateFunction(img, sect, 1);
			#else
				validateFunction(img, sect, 0);
			#endif
            
			if (img->errorCode)
				{
				// RIC DEBUG
				DMESG("ERROR validando Function section %d",numseccion);
				
				#ifndef VALIDATE_FUNCTIONS
					// try again with debug
					validateFunction(img, sect, 1);
				#endif
				
                return img;
				}
			else
				{
				// RIC DEBUG
				VALIDATE_FUNCTIONS_DBG("Function section %d VALIDATE ok !!!",numseccion);
				}
			
			}
			
		numseccion++;
    
		}
	
    return NULL;
}

// RIC
static VMImage *checkVTables(VMImage *img)
{
	
	FOR_SECTIONS()
		{
        
		auto vt = vtFor(sect);
        
		if (vt)
			{
            CHECK(((RefObject *)vmLiteralVal(sect))->vtable == vt, 1057);
			}
        
		if (sect->type == SectionType::Literal)
			{
            CHECK(vt != NULL, 1043);
			}
		else if (sect->type == SectionType::VTable)
			{
            auto vt = (VTable *)sect->data;
            CHECK(vt->methods[0] == (void *)pxt::RefRecord_destroy, 1058);
            CHECK(vt->methods[1] == (void *)pxt::RefRecord_print, 1058);
            CHECK(vt->methods[2] == (void *)pxt::RefRecord_scan, 1058);
            CHECK(vt->methods[3] == (void *)pxt::RefRecord_gcsize, 1058);
			}
		
		}
		
    return NULL;
}

// RIC
VMImage *loadVMImage(void *data, unsigned length)
{
	// RIC DEBUG: arrancamos aqui el contador de RAM fija que consume el
	// loader (estructuras auxiliares, no el heap del GC)
	g_imgLoadBytes = 0;
	
    auto img = new VMImage();
    memset(img, 0, sizeof(*img));

	// RIC DEBUG
	DMESG("");
    DMESG("START - loading and validanting .PXT64 image with %d bytes",length);
	
	// el propio struct VMImage tambien es RAM fija reservada por el loader
	g_imgLoadBytes += sizeof(VMImage);
	DMESG("    [MEM-IMG] %s: %d elementos x %d bytes = %d bytes (acumulado carga = d bytes)","VMImage (struct)", 1, (unsigned)sizeof(VMImage), (unsigned)sizeof(VMImage), g_imgLoadBytes);
	
    CHECK_AT(ALIGNED((uintptr_t)data), 1000, 0);
    CHECK_AT(ALIGNED(length), 1001, 0);

    img->dataStart = (uint64_t *)data;
    img->dataEnd = (uint64_t *)((uint8_t *)data + length);
	
    if ( countSections(img) )		{ /* error! */  return img; }
		DMESG("    image countSections OK");
    if ( checkVTables(img) )		{ /* error! */  return img; }
		DMESG("    image checkVTables OK");
    if ( loadSections(img) )		{ /* error! */  return img; }
		DMESG("    image loadSections OK");
    if ( loadIfaceNames(img) )		{ /* error! */  return img; }
		DMESG("    image loadIfaceNames OK");
    if ( validateFunctions(img) )	{ /* error! */  return img; }
		DMESG("    image validateFunctions OK");

	// RIC DEBUG	
	DMESG("    [MEM-IMG] TOTAL RAM fija reservada por el loader (sin contar GC): %d bytes", g_imgLoadBytes);
    DMESG("END - image loaded OK");
	
    return img;
}

void unloadVMImage(VMImage *img) {
    if (!img)
        return;

    free(img->pointerLiterals);
    free(img->sections);
    free(img->opcodes);
    free(img->opcodeDescs);
	#ifdef VM_COST_ENABLED
		free(img->opcodeCostNs);
	#endif
    free(img->numberLiterals);
    free(img->ifaceMemberNames);

    free(img->dataStart);
    memset(img, 0, sizeof(*img));
    delete img;
}

} // namespace pxt
