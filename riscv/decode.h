// See LICENSE for license details.

#ifndef _RISCV_DECODE_H
#define _RISCV_DECODE_H

#if (-1 != ~0) || ((-1 >> 1) != -1)
# error spike requires a two''s-complement c++ implementation
#endif

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include <string.h>
#include "pcr.h"
#include "config.h"
#include "common.h"
#include <cinttypes>

typedef int int128_t __attribute__((mode(TI)));
typedef unsigned int uint128_t __attribute__((mode(TI)));

typedef int64_t sreg_t;
typedef uint64_t reg_t;
typedef uint64_t freg_t;

const int NXPR = 32;
const int NFPR = 32;

#define FP_RD_NE  0
#define FP_RD_0   1
#define FP_RD_DN  2
#define FP_RD_UP  3
#define FP_RD_NMM 4

#define FSR_RD_SHIFT 5
#define FSR_RD   (0x7 << FSR_RD_SHIFT)

#define FPEXC_NX 0x01
#define FPEXC_UF 0x02
#define FPEXC_OF 0x04
#define FPEXC_DZ 0x08
#define FPEXC_NV 0x10

#define FSR_AEXC_SHIFT 0
#define FSR_NVA  (FPEXC_NV << FSR_AEXC_SHIFT)
#define FSR_OFA  (FPEXC_OF << FSR_AEXC_SHIFT)
#define FSR_UFA  (FPEXC_UF << FSR_AEXC_SHIFT)
#define FSR_DZA  (FPEXC_DZ << FSR_AEXC_SHIFT)
#define FSR_NXA  (FPEXC_NX << FSR_AEXC_SHIFT)
#define FSR_AEXC (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

#define FSR_ZERO ~(FSR_RD | FSR_AEXC)

class insn_t
{
public:
  uint32_t bits() { return b; }
  reg_t i_imm() { return int64_t(int32_t(b) >> 20); }
  reg_t s_imm() { return x(7, 5) | (x(25, 7) << 5) | (imm_sign() << 12); }
  reg_t sb_imm() { return (x(8, 4) << 1) | (x(25,6) << 5) | (x(7,1) << 11) | (imm_sign() << 12); }
  reg_t u_imm() { return int64_t(int32_t(b) >> 12 << 12); }
  reg_t uj_imm() { return (x(21, 10) << 1) | (x(20, 1) << 11) | (x(12, 8) << 12) | (imm_sign() << 20); }
  uint32_t rd() { return x(7, 5); }
  uint32_t rs1() { return x(15, 5); }
  uint32_t rs2() { return x(20, 5); }
  uint32_t rs3() { return x(27, 5); }
  uint32_t rm() { return x(12, 3); }
private:
  uint32_t b;
  reg_t x(int lo, int len) { return b << (32-lo-len) >> (32-len); }
  reg_t imm_sign() { return int64_t(int32_t(b) >> 31); }
};

template <class T, size_t N, bool zero_reg>
class regfile_t
{
public:
  void reset()
  {
    memset(data, 0, sizeof(data));
  }
  void write(size_t i, T value)
  {
    data[i] = value;
  }
  const T& operator [] (size_t i) const
  {
    if (zero_reg)
      const_cast<T&>(data[0]) = 0;
    return data[i];
  }
private:
  T data[N];
};

// helpful macros, etc
#define MMU (*p->get_mmu())
#define RS1 p->get_state()->XPR[insn.rs1()]
#define RS2 p->get_state()->XPR[insn.rs2()]
#define WRITE_RD(value) p->get_state()->XPR.write(insn.rd(), value)

#ifdef RISCV_ENABLE_COMMITLOG
  #undef WRITE_RD 
  #define WRITE_RD(value) ({ \
        bool in_spvr = p->get_state()->sr & SR_S; \
        reg_t wdata = value; /* value is a func with side-effects */ \
        if (!in_spvr) \
          fprintf(stderr, "x%u 0x%016" PRIx64, insn.rd(), ((uint64_t) wdata)); \
        p->get_state()->XPR.write(insn.rd(), wdata); \
      })
#endif

#define FRS1 p->get_state()->FPR[insn.rs1()]
#define FRS2 p->get_state()->FPR[insn.rs2()]
#define FRS3 p->get_state()->FPR[insn.rs3()]
#define WRITE_FRD(value) p->get_state()->FPR.write(insn.rd(), value)
 
#ifdef RISCV_ENABLE_COMMITLOG
  #undef WRITE_FRD 
  #define WRITE_FRD(value) ({ \
        bool in_spvr = p->get_state()->sr & SR_S; \
        freg_t wdata = value; /* value is a func with side-effects */ \
        if (!in_spvr) \
          fprintf(stderr, "f%u 0x%016" PRIx64, insn.rd(), ((uint64_t) wdata)); \
        p->get_state()->FPR.write(insn.rd(), wdata); \
      })
#endif
 


#define SHAMT (insn.i_imm() & 0x3F)
#define BRANCH_TARGET (pc + insn.sb_imm())
#define JUMP_TARGET (pc + insn.uj_imm())
#define RM ({ int rm = insn.rm(); \
              if(rm == 7) rm = (p->get_state()->fsr & FSR_RD) >> FSR_RD_SHIFT; \
              if(rm > 4) throw trap_illegal_instruction(); \
              rm; })

#define xpr64 (xprlen == 64)

#define require_supervisor if(unlikely(!(p->get_state()->sr & SR_S))) throw trap_privileged_instruction()
#define require_xpr64 if(unlikely(!xpr64)) throw trap_illegal_instruction()
#define require_xpr32 if(unlikely(xpr64)) throw trap_illegal_instruction()
#ifndef RISCV_ENABLE_FPU
# define require_fp throw trap_illegal_instruction()
#else
# define require_fp if(unlikely(!(p->get_state()->sr & SR_EF))) throw trap_fp_disabled()
#endif
#define require_accelerator if(unlikely(!(p->get_state()->sr & SR_EA))) throw trap_accelerator_disabled()

#define cmp_trunc(reg) (reg_t(reg) << (64-xprlen))
#define set_fp_exceptions ({ p->set_fsr(p->get_state()->fsr | \
                               (softfloat_exceptionFlags << FSR_AEXC_SHIFT)); \
                             softfloat_exceptionFlags = 0; })

#define sext32(x) ((sreg_t)(int32_t)(x))
#define zext32(x) ((reg_t)(uint32_t)(x))
#define sext_xprlen(x) (((sreg_t)(x) << (64-xprlen)) >> (64-xprlen))
#define zext_xprlen(x) (((reg_t)(x) << (64-xprlen)) >> (64-xprlen))

#define insn_length(x) \
  (((x) & 0x03) < 0x03 ? 2 : \
   ((x) & 0x1f) < 0x1f ? 4 : \
   ((x) & 0x3f) < 0x3f ? 6 : \
   8)

#define set_pc(x) \
  do { if ((x) & 3 /* For now... */) \
         throw trap_instruction_address_misaligned(); \
       npc = (x); \
     } while(0)

#endif
