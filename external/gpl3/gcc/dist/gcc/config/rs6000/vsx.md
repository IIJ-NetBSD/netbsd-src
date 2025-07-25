;; VSX patterns.
;; Copyright (C) 2009-2022 Free Software Foundation, Inc.
;; Contributed by Michael Meissner <meissner@linux.vnet.ibm.com>

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; Iterator for comparison types
(define_code_iterator CMP_TEST [eq lt gt unordered])

;; Mode attribute for vector floate and floato conversions
(define_mode_attr VF_sxddp [(V2DI "sxd") (V2DF "dp")])

;; Iterator for both scalar and vector floating point types supported by VSX
(define_mode_iterator VSX_B [DF V4SF V2DF])

;; Iterator for the 2 64-bit vector types
(define_mode_iterator VSX_D [V2DF V2DI])

;; Mode iterator to handle swapping words on little endian for the 128-bit
;; types that goes in a single vector register.
(define_mode_iterator VSX_LE_128 [(KF   "FLOAT128_VECTOR_P (KFmode)")
				  (TF   "FLOAT128_VECTOR_P (TFmode)")
				  TI
				  V1TI])

;; Iterator for the 2 32-bit vector types
(define_mode_iterator VSX_W [V4SF V4SI])

;; Iterator for the DF types
(define_mode_iterator VSX_DF [V2DF DF])

;; Iterator for vector floating point types supported by VSX
(define_mode_iterator VSX_F [V4SF V2DF])

;; Iterator for logical types supported by VSX
(define_mode_iterator VSX_L [V16QI
			     V8HI
			     V4SI
			     V2DI
			     V4SF
			     V2DF
			     V1TI
			     TI
			     (KF	"FLOAT128_VECTOR_P (KFmode)")
			     (TF	"FLOAT128_VECTOR_P (TFmode)")])

;; Iterator for memory moves.
(define_mode_iterator VSX_M [V16QI
			     V8HI
			     V4SI
			     V2DI
			     V4SF
			     V2DF
			     V1TI
			     (KF	"FLOAT128_VECTOR_P (KFmode)")
			     (TF	"FLOAT128_VECTOR_P (TFmode)")
			     TI])

(define_mode_attr VSX_XXBR  [(V8HI  "h")
			     (V4SI  "w")
			     (V4SF  "w")
			     (V2DF  "d")
			     (V2DI  "d")
			     (V1TI  "q")])

;; Map into the appropriate load/store name based on the type
(define_mode_attr VSm  [(V16QI "vw4")
			(V8HI  "vw4")
			(V4SI  "vw4")
			(V4SF  "vw4")
			(V2DF  "vd2")
			(V2DI  "vd2")
			(DF    "d")
			(TF    "vd2")
			(KF    "vd2")
			(V1TI  "vd2")
			(TI    "vd2")])

;; Map the register class used
(define_mode_attr VSr	[(V16QI "v")
			 (V8HI  "v")
			 (V4SI  "v")
			 (V4SF  "wa")
			 (V2DI  "wa")
			 (V2DF  "wa")
			 (DI	"wa")
			 (DF    "wa")
			 (SF	"wa")
			 (TF	"wa")
			 (KF	"wa")
			 (V1TI  "v")
			 (TI    "wa")])

;; What value we need in the "isa" field, to make the IEEE QP float work.
(define_mode_attr VSisa	[(V16QI "*")
			 (V8HI  "*")
			 (V4SI  "*")
			 (V4SF  "*")
			 (V2DI  "*")
			 (V2DF  "*")
			 (DI	"*")
			 (DF    "*")
			 (SF	"*")
			 (V1TI	"*")
			 (TI    "*")
			 (TF	"p9tf")
			 (KF	"p9kf")])

;; A mode attribute to disparage use of GPR registers, except for scalar
;; integer modes.
(define_mode_attr ??r	[(V16QI	"??r")
			 (V8HI	"??r")
			 (V4SI	"??r")
			 (V4SF	"??r")
			 (V2DI	"??r")
			 (V2DF	"??r")
			 (V1TI	"??r")
			 (KF	"??r")
			 (TF	"??r")
			 (TI	"r")])

;; A mode attribute used for 128-bit constant values.
(define_mode_attr nW	[(V16QI	"W")
			 (V8HI	"W")
			 (V4SI	"W")
			 (V4SF	"W")
			 (V2DI	"W")
			 (V2DF	"W")
			 (V1TI	"W")
			 (KF	"W")
			 (TF	"W")
			 (TI	"n")])

;; Same size integer type for floating point data
(define_mode_attr VSi [(V4SF  "v4si")
		       (V2DF  "v2di")
		       (DF    "di")])

(define_mode_attr VSI [(V4SF  "V4SI")
		       (V2DF  "V2DI")
		       (DF    "DI")])

;; Word size for same size conversion
(define_mode_attr VSc [(V4SF "w")
		       (V2DF "d")
		       (DF   "d")])

;; Map into either s or v, depending on whether this is a scalar or vector
;; operation
(define_mode_attr VSv	[(V16QI "v")
			 (V8HI  "v")
			 (V4SI  "v")
			 (V4SF  "v")
			 (V2DI  "v")
			 (V2DF  "v")
			 (V1TI  "v")
			 (DF    "s")
			 (KF	"v")])

;; Appropriate type for add ops (and other simple FP ops)
(define_mode_attr VStype_simple	[(V2DF "vecdouble")
				 (V4SF "vecfloat")
				 (DF   "fp")])

;; Appropriate type for multiply ops
(define_mode_attr VStype_mul	[(V2DF "vecdouble")
				 (V4SF "vecfloat")
				 (DF   "dmul")])

;; Appropriate type for divide ops.
(define_mode_attr VStype_div	[(V2DF "vecdiv")
				 (V4SF "vecfdiv")
				 (DF   "ddiv")])

;; Map the scalar mode for a vector type
(define_mode_attr VS_scalar [(V1TI	"TI")
			     (V2DF	"DF")
			     (V2DI	"DI")
			     (V4SF	"SF")
			     (V4SI	"SI")
			     (V8HI	"HI")
			     (V16QI	"QI")])

;; Map to a double-sized vector mode
(define_mode_attr VS_double [(V4SI	"V8SI")
			     (V4SF	"V8SF")
			     (V2DI	"V4DI")
			     (V2DF	"V4DF")
			     (V1TI	"V2TI")])

;; Iterators for loading constants with xxspltib
(define_mode_iterator VSINT_84  [V4SI V2DI DI SI])
(define_mode_iterator VSINT_842 [V8HI V4SI V2DI])

;; Vector reverse byte modes
(define_mode_iterator VEC_REVB [V8HI V4SI V2DI V4SF V2DF V1TI])

;; Iterator for ISA 3.0 vector extract/insert of small integer vectors.
;; VSX_EXTRACT_I2 doesn't include V4SImode because SI extracts can be
;; done on ISA 2.07 and not just ISA 3.0.
(define_mode_iterator VSX_EXTRACT_I  [V16QI V8HI V4SI])
(define_mode_iterator VSX_EXTRACT_I2 [V16QI V8HI])
(define_mode_iterator VSX_EXTRACT_I4 [V16QI V8HI V4SI V2DI])

(define_mode_attr VSX_EXTRACT_WIDTH [(V16QI "b")
		  		     (V8HI "h")
				     (V4SI "w")])

;; Mode attribute to give the correct predicate for ISA 3.0 vector extract and
;; insert to validate the operand number.
(define_mode_attr VSX_EXTRACT_PREDICATE [(V16QI "const_0_to_15_operand")
					 (V8HI  "const_0_to_7_operand")
					 (V4SI  "const_0_to_3_operand")])

;; Mode attribute to give the constraint for vector extract and insert
;; operations.
(define_mode_attr VSX_EX [(V16QI "v")
			  (V8HI  "v")
			  (V4SI  "wa")])

;; Mode iterator for binary floating types other than double to
;; optimize convert to that floating point type from an extract
;; of an integer type
(define_mode_iterator VSX_EXTRACT_FL [SF
				      (IF "FLOAT128_2REG_P (IFmode)")
				      (KF "TARGET_FLOAT128_HW")
				      (TF "FLOAT128_2REG_P (TFmode)
					   || (FLOAT128_IEEE_P (TFmode)
					       && TARGET_FLOAT128_HW)")])

;; Mode iterator for binary floating types that have a direct conversion
;; from 64-bit integer to floating point
(define_mode_iterator FL_CONV [SF
			       DF
			       (KF "TARGET_FLOAT128_HW")
			       (TF "TARGET_FLOAT128_HW
				    && FLOAT128_IEEE_P (TFmode)")])

;; Iterator for the 2 short vector types to do a splat from an integer
(define_mode_iterator VSX_SPLAT_I [V16QI V8HI])

;; Mode attribute to give the count for the splat instruction to splat
;; the value in the 64-bit integer slot
(define_mode_attr VSX_SPLAT_COUNT [(V16QI "7") (V8HI "3")])

;; Mode attribute to give the suffix for the splat instruction
(define_mode_attr VSX_SPLAT_SUFFIX [(V16QI "b") (V8HI "h")])

;; Iterator for the move to mask instructions
(define_mode_iterator VSX_MM [V16QI V8HI V4SI V2DI V1TI])
(define_mode_iterator VSX_MM4 [V16QI V8HI V4SI V2DI])

;; Longer vec int modes for rotate/mask ops
;; and Vector Integer Multiply/Divide/Modulo Instructions
(define_mode_iterator VIlong [V2DI V4SI])

;; Constants for creating unspecs
(define_c_enum "unspec"
  [UNSPEC_VSX_CONCAT
   UNSPEC_VSX_CVDPSXWS
   UNSPEC_VSX_CVDPUXWS
   UNSPEC_VSX_CVSPDP
   UNSPEC_VSX_CVHPSP
   UNSPEC_VSX_CVSPDPN
   UNSPEC_VSX_CVDPSPN
   UNSPEC_VSX_CVSXWDP
   UNSPEC_VSX_CVUXWDP
   UNSPEC_VSX_CVSXDSP
   UNSPEC_VSX_CVUXDSP
   UNSPEC_VSX_FLOAT2
   UNSPEC_VSX_UNS_FLOAT2
   UNSPEC_VSX_FLOATE
   UNSPEC_VSX_UNS_FLOATE
   UNSPEC_VSX_FLOATO
   UNSPEC_VSX_UNS_FLOATO
   UNSPEC_VSX_TDIV
   UNSPEC_VSX_TSQRT
   UNSPEC_VSX_SET
   UNSPEC_VSX_ROUND_I
   UNSPEC_VSX_ROUND_IC
   UNSPEC_VSX_SLDWI
   UNSPEC_VSX_XXPERM

   UNSPEC_VSX_XXSPLTW
   UNSPEC_VSX_XXSPLTD
   UNSPEC_VSX_DIVSD
   UNSPEC_VSX_DIVUD
   UNSPEC_VSX_DIVSQ
   UNSPEC_VSX_DIVUQ
   UNSPEC_VSX_DIVESQ
   UNSPEC_VSX_DIVEUQ
   UNSPEC_VSX_MODSQ
   UNSPEC_VSX_MODUQ
   UNSPEC_VSX_MULSD
   UNSPEC_VSX_SIGN_EXTEND
   UNSPEC_VSX_XVCVBF16SPN
   UNSPEC_VSX_XVCVSPBF16
   UNSPEC_VSX_XVCVSPSXDS
   UNSPEC_VSX_XVCVSPHP
   UNSPEC_VSX_VSLO
   UNSPEC_VSX_EXTRACT
   UNSPEC_VSX_SXEXPDP
   UNSPEC_VSX_SXSIG
   UNSPEC_VSX_SIEXPDP
   UNSPEC_VSX_SIEXPQP
   UNSPEC_VSX_SCMPEXPDP
   UNSPEC_VSX_SCMPEXPQP
   UNSPEC_VSX_STSTDC
   UNSPEC_VSX_VEXTRACT_FP_FROM_SHORTH
   UNSPEC_VSX_VEXTRACT_FP_FROM_SHORTL
   UNSPEC_VSX_VXEXP
   UNSPEC_VSX_VXSIG
   UNSPEC_VSX_VIEXP
   UNSPEC_VSX_VTSTDC
   UNSPEC_VSX_VSIGNED2

   UNSPEC_LXVL
   UNSPEC_LXVLL
   UNSPEC_LVSL_REG
   UNSPEC_LVSR_REG
   UNSPEC_STXVL
   UNSPEC_STXVLL
   UNSPEC_XL_LEN_R
   UNSPEC_XST_LEN_R

   UNSPEC_VCLZLSBB
   UNSPEC_VCTZLSBB
   UNSPEC_VEXTUBLX
   UNSPEC_VEXTUHLX
   UNSPEC_VEXTUWLX
   UNSPEC_VEXTUBRX
   UNSPEC_VEXTUHRX
   UNSPEC_VEXTUWRX
   UNSPEC_VCMPNEB
   UNSPEC_VCMPNEZB
   UNSPEC_VCMPNEH
   UNSPEC_VCMPNEZH
   UNSPEC_VCMPNEW
   UNSPEC_VCMPNEZW
   UNSPEC_XXEXTRACTUW
   UNSPEC_XXINSERTW
   UNSPEC_VSX_FIRST_MATCH_INDEX
   UNSPEC_VSX_FIRST_MATCH_EOS_INDEX
   UNSPEC_VSX_FIRST_MISMATCH_INDEX
   UNSPEC_VSX_FIRST_MISMATCH_EOS_INDEX
   UNSPEC_XXGENPCV
   UNSPEC_MTVSBM
   UNSPEC_EXTENDDITI2
   UNSPEC_VCNTMB
   UNSPEC_VEXPAND
   UNSPEC_VEXTRACT
   UNSPEC_EXTRACTL
   UNSPEC_EXTRACTR
   UNSPEC_INSERTL
   UNSPEC_INSERTR
   UNSPEC_REPLACE_ELT
   UNSPEC_REPLACE_UN
   UNSPEC_VDIVES
   UNSPEC_VDIVEU
   UNSPEC_VMSUMCUD
   UNSPEC_XXEVAL
   UNSPEC_XXSPLTIW
   UNSPEC_XXSPLTIDP
   UNSPEC_XXSPLTI32DX
   UNSPEC_XXBLEND
   UNSPEC_XXPERMX
  ])

(define_int_iterator XVCVBF16	[UNSPEC_VSX_XVCVSPBF16
				 UNSPEC_VSX_XVCVBF16SPN])

(define_int_attr xvcvbf16       [(UNSPEC_VSX_XVCVSPBF16 "xvcvspbf16")
				 (UNSPEC_VSX_XVCVBF16SPN "xvcvbf16spn")])

;; Like VI, defined in vector.md, but add ISA 2.07 integer vector ops
(define_mode_iterator VI2 [V4SI V8HI V16QI V2DI])

;; Vector extract_elt iterator/attr for 32-bit and 64-bit elements
(define_mode_iterator REPLACE_ELT [V4SI V4SF V2DI V2DF])
(define_mode_attr REPLACE_ELT_char [(V4SI "w") (V4SF "w")
				    (V2DI  "d") (V2DF "d")])
(define_mode_attr REPLACE_ELT_sh [(V4SI "2") (V4SF "2")
				  (V2DI  "3") (V2DF "3")])
(define_mode_attr REPLACE_ELT_max [(V4SI "12") (V4SF "12")
				   (V2DI  "8") (V2DF "8")])

;; Like VM2 in altivec.md, just do char, short, int, long, float and double
(define_mode_iterator VM3 [V4SI
			   V8HI
			   V16QI
			   V4SF
			   V2DF
			   V2DI])

(define_mode_attr VM3_char [(V2DI "d")
			   (V4SI "w")
			   (V8HI "h")
			   (V16QI "b")
			   (V2DF  "d")
			   (V4SF  "w")])


;; VSX moves

;; The patterns for LE permuted loads and stores come before the general
;; VSX moves so they match first.
(define_insn_and_split "*vsx_le_perm_load_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
        (match_operand:VSX_D 1 "indexed_or_indirect_operand" "Z"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  "&& 1"
  [(set (match_dup 2)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 1) (const_int 0)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 2)
          (parallel [(const_int 1) (const_int 0)])))]
{
  rtx mem = operands[1];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register destination is not in the altivec
     range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_NUM_P (reg_or_subregno (operands[0]))
	  || ALTIVEC_REGNO_P (reg_or_subregno (operands[0]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);

      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
        {
	  /* Replace the source memory address with masked address.  */
          rtx lvx_set_expr = rs6000_gen_lvx (mode, operands[0], mem);
	  emit_insn (lvx_set_expr);
	  DONE;
        }
      else if (rs6000_quadword_masked_address_p (mem_address))
        {
	  /* This rtl is already in the form that matches lvx
	     instruction, so leave it alone.  */
	  DONE;
        }
      /* Otherwise, fall through to transform into a swapping load.  */
    }
  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[0])
                                       : operands[0];
}
  [(set_attr "type" "vecload")
   (set_attr "length" "8")])

(define_insn_and_split "*vsx_le_perm_load_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
        (match_operand:VSX_W 1 "indexed_or_indirect_operand" "Z"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  "&& 1"
  [(set (match_dup 2)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 2) (const_int 3)
                     (const_int 0) (const_int 1)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 2)
          (parallel [(const_int 2) (const_int 3)
                     (const_int 0) (const_int 1)])))]
{
  rtx mem = operands[1];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register destination is not in the altivec
     range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_P (operands[0])
	  || ALTIVEC_REGNO_P (REGNO(operands[0]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);

      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
        {
	  /* Replace the source memory address with masked address.  */
          rtx lvx_set_expr = rs6000_gen_lvx (mode, operands[0], mem);
	  emit_insn (lvx_set_expr);
	  DONE;
        }
      else if (rs6000_quadword_masked_address_p (mem_address))
        {
	  /* This rtl is already in the form that matches lvx
	     instruction, so leave it alone.  */
	  DONE;
        }
      /* Otherwise, fall through to transform into a swapping load.  */
    }
  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[0])
                                       : operands[0];
}
  [(set_attr "type" "vecload")
   (set_attr "length" "8")])

(define_insn_and_split "*vsx_le_perm_load_v8hi"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
        (match_operand:V8HI 1 "indexed_or_indirect_operand" "Z"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  "&& 1"
  [(set (match_dup 2)
        (vec_select:V8HI
          (match_dup 1)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))
   (set (match_dup 0)
        (vec_select:V8HI
          (match_dup 2)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))]
{
  rtx mem = operands[1];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register destination is not in the altivec
     range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_P (operands[0])
	  || ALTIVEC_REGNO_P (REGNO(operands[0]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);

      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
        {
	  /* Replace the source memory address with masked address.  */
	  rtx lvx_set_expr = rs6000_gen_lvx (mode, operands[0], mem);
	  emit_insn (lvx_set_expr);
	  DONE;
        }
      else if (rs6000_quadword_masked_address_p (mem_address))
        {
	  /* This rtl is already in the form that matches lvx
	     instruction, so leave it alone.  */
	  DONE;
        }
      /* Otherwise, fall through to transform into a swapping load.  */
    }
  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[0])
                                       : operands[0];
}
  [(set_attr "type" "vecload")
   (set_attr "length" "8")])

(define_insn_and_split "*vsx_le_perm_load_v16qi"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
        (match_operand:V16QI 1 "indexed_or_indirect_operand" "Z"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  "&& 1"
  [(set (match_dup 2)
        (vec_select:V16QI
          (match_dup 1)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))
   (set (match_dup 0)
        (vec_select:V16QI
          (match_dup 2)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))]
{
  rtx mem = operands[1];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register destination is not in the altivec
     range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_P (operands[0])
	  || ALTIVEC_REGNO_P (REGNO(operands[0]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);

      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
        {
	  /* Replace the source memory address with masked address.  */
	  rtx lvx_set_expr = rs6000_gen_lvx (mode, operands[0], mem);
	  emit_insn (lvx_set_expr);
	  DONE;
        }
      else if (rs6000_quadword_masked_address_p (mem_address))
        {
	  /* This rtl is already in the form that matches lvx
	     instruction, so leave it alone.  */
	  DONE;
        }
      /* Otherwise, fall through to transform into a swapping load.  */
    }
  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[0])
                                       : operands[0];
}
  [(set_attr "type" "vecload")
   (set_attr "length" "8")])

(define_insn "*vsx_le_perm_store_<mode>"
  [(set (match_operand:VSX_D 0 "indexed_or_indirect_operand" "=Z")
        (match_operand:VSX_D 1 "vsx_register_operand" "+wa"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  [(set_attr "type" "vecstore")
   (set_attr "length" "12")])

(define_split
  [(set (match_operand:VSX_D 0 "indexed_or_indirect_operand")
        (match_operand:VSX_D 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && !reload_completed"
  [(set (match_dup 2)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 1) (const_int 0)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 2)
          (parallel [(const_int 1) (const_int 0)])))]
{
  rtx mem = operands[0];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register source is not in the altivec range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_NUM_P (reg_or_subregno (operands[1]))
	  || ALTIVEC_REGNO_P (reg_or_subregno (operands[1]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);
      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
	{
	  rtx stvx_set_expr = rs6000_gen_stvx (mode, mem, operands[1]);
	  emit_insn (stvx_set_expr);
	  DONE;
	}
      else if (rs6000_quadword_masked_address_p (mem_address))
	{
	  /* This rtl is already in the form that matches stvx instruction,
	     so leave it alone.  */
	  DONE;
	}
      /* Otherwise, fall through to transform into a swapping store.  */
    }

  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[1]) 
                                       : operands[1];
})

;; The post-reload split requires that we re-permute the source
;; register in case it is still live.
(define_split
  [(set (match_operand:VSX_D 0 "indexed_or_indirect_operand")
        (match_operand:VSX_D 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && reload_completed"
  [(set (match_dup 1)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 1) (const_int 0)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 1) (const_int 0)])))
   (set (match_dup 1)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 1) (const_int 0)])))]
  "")

(define_insn "*vsx_le_perm_store_<mode>"
  [(set (match_operand:VSX_W 0 "indexed_or_indirect_operand" "=Z")
        (match_operand:VSX_W 1 "vsx_register_operand" "+wa"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  [(set_attr "type" "vecstore")
   (set_attr "length" "12")])

(define_split
  [(set (match_operand:VSX_W 0 "indexed_or_indirect_operand")
        (match_operand:VSX_W 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && !reload_completed"
  [(set (match_dup 2)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 2) (const_int 3)
	             (const_int 0) (const_int 1)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 2)
          (parallel [(const_int 2) (const_int 3)
	             (const_int 0) (const_int 1)])))]
{
  rtx mem = operands[0];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register source is not in the altivec range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_NUM_P (reg_or_subregno (operands[1]))
	  || ALTIVEC_REGNO_P (reg_or_subregno (operands[1]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);
      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
	{
	  rtx stvx_set_expr = rs6000_gen_stvx (mode, mem, operands[1]);
	  emit_insn (stvx_set_expr);
	  DONE;
	}
      else if (rs6000_quadword_masked_address_p (mem_address))
	{
	  /* This rtl is already in the form that matches stvx instruction,
	     so leave it alone.  */
	  DONE;
	}
      /* Otherwise, fall through to transform into a swapping store.  */
    }

  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[1]) 
                                       : operands[1];
})

;; The post-reload split requires that we re-permute the source
;; register in case it is still live.
(define_split
  [(set (match_operand:VSX_W 0 "indexed_or_indirect_operand")
        (match_operand:VSX_W 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && reload_completed"
  [(set (match_dup 1)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 2) (const_int 3)
	             (const_int 0) (const_int 1)])))
   (set (match_dup 0)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 2) (const_int 3)
	             (const_int 0) (const_int 1)])))
   (set (match_dup 1)
        (vec_select:<MODE>
          (match_dup 1)
          (parallel [(const_int 2) (const_int 3)
	             (const_int 0) (const_int 1)])))]
  "")

(define_insn "*vsx_le_perm_store_v8hi"
  [(set (match_operand:V8HI 0 "indexed_or_indirect_operand" "=Z")
        (match_operand:V8HI 1 "vsx_register_operand" "+wa"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  [(set_attr "type" "vecstore")
   (set_attr "length" "12")])

(define_split
  [(set (match_operand:V8HI 0 "indexed_or_indirect_operand")
        (match_operand:V8HI 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && !reload_completed"
  [(set (match_dup 2)
        (vec_select:V8HI
          (match_dup 1)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))
   (set (match_dup 0)
        (vec_select:V8HI
          (match_dup 2)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))]
{
  rtx mem = operands[0];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register source is not in the altivec range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_NUM_P (reg_or_subregno (operands[1]))
	  || ALTIVEC_REGNO_P (reg_or_subregno (operands[1]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);
      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
	{
	  rtx stvx_set_expr = rs6000_gen_stvx (mode, mem, operands[1]);
	  emit_insn (stvx_set_expr);
	  DONE;
	}
      else if (rs6000_quadword_masked_address_p (mem_address))
	{
	  /* This rtl is already in the form that matches stvx instruction,
	     so leave it alone.  */
	  DONE;
	}
      /* Otherwise, fall through to transform into a swapping store.  */
    }

  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[1]) 
                                       : operands[1];
})

;; The post-reload split requires that we re-permute the source
;; register in case it is still live.
(define_split
  [(set (match_operand:V8HI 0 "indexed_or_indirect_operand")
        (match_operand:V8HI 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && reload_completed"
  [(set (match_dup 1)
        (vec_select:V8HI
          (match_dup 1)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))
   (set (match_dup 0)
        (vec_select:V8HI
          (match_dup 1)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))
   (set (match_dup 1)
        (vec_select:V8HI
          (match_dup 1)
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))]
  "")

(define_insn "*vsx_le_perm_store_v16qi"
  [(set (match_operand:V16QI 0 "indexed_or_indirect_operand" "=Z")
        (match_operand:V16QI 1 "vsx_register_operand" "+wa"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "#"
  [(set_attr "type" "vecstore")
   (set_attr "length" "12")])

(define_split
  [(set (match_operand:V16QI 0 "indexed_or_indirect_operand")
        (match_operand:V16QI 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && !reload_completed"
  [(set (match_dup 2)
        (vec_select:V16QI
          (match_dup 1)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))
   (set (match_dup 0)
        (vec_select:V16QI
          (match_dup 2)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))]
{
  rtx mem = operands[0];

  /* Don't apply the swap optimization if we've already performed register
     allocation and the hard register source is not in the altivec range.  */
  if ((MEM_ALIGN (mem) >= 128)
      && (!HARD_REGISTER_NUM_P (reg_or_subregno (operands[1]))
	  || ALTIVEC_REGNO_P (reg_or_subregno (operands[1]))))
    {
      rtx mem_address = XEXP (mem, 0);
      enum machine_mode mode = GET_MODE (mem);
      if (REG_P (mem_address) || rs6000_sum_of_two_registers_p (mem_address))
	{
	  rtx stvx_set_expr = rs6000_gen_stvx (mode, mem, operands[1]);
	  emit_insn (stvx_set_expr);
	  DONE;
	}
      else if (rs6000_quadword_masked_address_p (mem_address))
	{
	  /* This rtl is already in the form that matches stvx instruction,
	     so leave it alone.  */
	  DONE;
	}
      /* Otherwise, fall through to transform into a swapping store.  */
    }

  operands[2] = can_create_pseudo_p () ? gen_reg_rtx_and_attrs (operands[1]) 
                                       : operands[1];
})

;; The post-reload split requires that we re-permute the source
;; register in case it is still live.
(define_split
  [(set (match_operand:V16QI 0 "indexed_or_indirect_operand")
        (match_operand:V16QI 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR && reload_completed"
  [(set (match_dup 1)
        (vec_select:V16QI
          (match_dup 1)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))
   (set (match_dup 0)
        (vec_select:V16QI
          (match_dup 1)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))
   (set (match_dup 1)
        (vec_select:V16QI
          (match_dup 1)
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))]
  "")

;; Little endian word swapping for 128-bit types that are either scalars or the
;; special V1TI container class, which it is not appropriate to use vec_select
;; for the type.
(define_insn "*vsx_le_permute_<mode>"
  [(set (match_operand:VEC_TI 0 "nonimmediate_operand" "=wa,wa,Z,&r,&r,Q")
	(rotate:VEC_TI
	 (match_operand:VEC_TI 1 "input_operand" "wa,Z,wa,r,Q,r")
	 (const_int 64)))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR"
  "@
   xxpermdi %x0,%x1,%x1,2
   lxvd2x %x0,%y1
   stxvd2x %x1,%y0
   mr %0,%L1\;mr %L0,%1
   ld%U1%X1 %0,%L1\;ld%U1%X1 %L0,%1
   std%U0%X0 %L1,%0\;std%U0%X0 %1,%L0"
  [(set_attr "length" "*,*,*,8,8,8")
   (set_attr "type" "vecperm,vecload,vecstore,*,load,store")])

(define_insn_and_split "*vsx_le_undo_permute_<mode>"
  [(set (match_operand:VEC_TI 0 "vsx_register_operand" "=wa,wa")
	(rotate:VEC_TI
	 (rotate:VEC_TI
	  (match_operand:VEC_TI 1 "vsx_register_operand" "0,wa")
	  (const_int 64))
	 (const_int 64)))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX"
  "@
   #
   xxlor %x0,%x1"
  "&& 1"
  [(set (match_dup 0) (match_dup 1))]
{
  if (reload_completed && REGNO (operands[0]) == REGNO (operands[1]))
    {
      emit_note (NOTE_INSN_DELETED);
      DONE;
    }
}
  [(set_attr "length" "0,4")
   (set_attr "type" "veclogical")])

(define_insn_and_split "*vsx_le_perm_load_<mode>"
  [(set (match_operand:VSX_LE_128 0 "vsx_register_operand" "=wa,r")
        (match_operand:VSX_LE_128 1 "memory_operand" "Z,Q"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && !altivec_indexed_or_indirect_operand (operands[1], <MODE>mode)"
  "@
   #
   #"
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && !altivec_indexed_or_indirect_operand (operands[1], <MODE>mode)"
  [(const_int 0)]
{
  rtx tmp = (can_create_pseudo_p ()
	     ? gen_reg_rtx_and_attrs (operands[0])
	     : operands[0]);
  rs6000_emit_le_vsx_permute (tmp, operands[1], <MODE>mode);
  rs6000_emit_le_vsx_permute (operands[0], tmp, <MODE>mode);
  DONE;
}
  [(set_attr "type" "vecload,load")
   (set_attr "length" "8,8")
   (set_attr "isa" "<VSisa>,*")])

(define_insn "*vsx_le_perm_store_<mode>"
  [(set (match_operand:VSX_LE_128 0 "memory_operand" "=Z,Q")
        (match_operand:VSX_LE_128 1 "vsx_register_operand" "+wa,r"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && !altivec_indexed_or_indirect_operand (operands[0], <MODE>mode)"
  "@
   #
   #"
  [(set_attr "type" "vecstore,store")
   (set_attr "length" "12,8")
   (set_attr "isa" "<VSisa>,*")])

(define_split
  [(set (match_operand:VSX_LE_128 0 "memory_operand")
        (match_operand:VSX_LE_128 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !reload_completed && !TARGET_P9_VECTOR
   && !altivec_indexed_or_indirect_operand (operands[0], <MODE>mode)"
  [(const_int 0)]
{
  rtx tmp = (can_create_pseudo_p ()
	     ? gen_reg_rtx_and_attrs (operands[0])
	     : operands[0]);
  rs6000_emit_le_vsx_permute (tmp, operands[1], <MODE>mode);
  rs6000_emit_le_vsx_permute (operands[0], tmp, <MODE>mode);
  DONE;
})

;; Peepholes to catch loads and stores for TImode if TImode landed in
;; GPR registers on a little endian system.
(define_peephole2
  [(set (match_operand:VEC_TI 0 "int_reg_operand")
	(rotate:VEC_TI (match_operand:VEC_TI 1 "memory_operand")
		       (const_int 64)))
   (set (match_operand:VEC_TI 2 "int_reg_operand")
	(rotate:VEC_TI (match_dup 0)
		       (const_int 64)))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && (rtx_equal_p (operands[0], operands[2])
       || peep2_reg_dead_p (2, operands[0]))"
   [(set (match_dup 2) (match_dup 1))])

(define_peephole2
  [(set (match_operand:VEC_TI 0 "int_reg_operand")
	(rotate:VEC_TI (match_operand:VEC_TI 1 "int_reg_operand")
		       (const_int 64)))
   (set (match_operand:VEC_TI 2 "memory_operand")
	(rotate:VEC_TI (match_dup 0)
		       (const_int 64)))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && peep2_reg_dead_p (2, operands[0])"
   [(set (match_dup 2) (match_dup 1))])

;; Peephole to catch memory to memory transfers for TImode if TImode landed in
;; VSX registers on a little endian system.  The vector types and IEEE 128-bit
;; floating point are handled by the more generic swap elimination pass.
(define_peephole2
  [(set (match_operand:TI 0 "vsx_register_operand")
	(rotate:TI (match_operand:TI 1 "vsx_register_operand")
		   (const_int 64)))
   (set (match_operand:TI 2 "vsx_register_operand")
	(rotate:TI (match_dup 0)
		   (const_int 64)))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && !TARGET_P9_VECTOR
   && (rtx_equal_p (operands[0], operands[2])
       || peep2_reg_dead_p (2, operands[0]))"
   [(set (match_dup 2) (match_dup 1))])

;; The post-reload split requires that we re-permute the source
;; register in case it is still live.
(define_split
  [(set (match_operand:VSX_LE_128 0 "memory_operand")
        (match_operand:VSX_LE_128 1 "vsx_register_operand"))]
  "!BYTES_BIG_ENDIAN && TARGET_VSX && reload_completed && !TARGET_P9_VECTOR
   && !altivec_indexed_or_indirect_operand (operands[0], <MODE>mode)"
  [(const_int 0)]
{
  rs6000_emit_le_vsx_permute (operands[1], operands[1], <MODE>mode);
  rs6000_emit_le_vsx_permute (operands[0], operands[1], <MODE>mode);
  rs6000_emit_le_vsx_permute (operands[1], operands[1], <MODE>mode);
  DONE;
})

;; Vector constants that can be generated with XXSPLTIB that was added in ISA
;; 3.0.  Both (const_vector [..]) and (vec_duplicate ...) forms are recognized.
(define_insn "xxspltib_v16qi"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(vec_duplicate:V16QI (match_operand:SI 1 "s8bit_cint_operand" "n")))]
  "TARGET_P9_VECTOR"
{
  operands[2] = GEN_INT (INTVAL (operands[1]) & 0xff);
  return "xxspltib %x0,%2";
}
  [(set_attr "type" "vecperm")])

(define_insn "xxspltib_<mode>_nosplit"
  [(set (match_operand:VSINT_842 0 "vsx_register_operand" "=wa,wa")
	(match_operand:VSINT_842 1 "xxspltib_constant_nosplit" "jwM,wE"))]
  "TARGET_P9_VECTOR"
{
  rtx op1 = operands[1];
  int value = 256;
  int num_insns = -1;

  if (!xxspltib_constant_p (op1, <MODE>mode, &num_insns, &value)
      || num_insns != 1)
    gcc_unreachable ();

  operands[2] = GEN_INT (value & 0xff);
  return "xxspltib %x0,%2";
}
  [(set_attr "type" "vecperm")])

(define_insn_and_split "*xxspltib_<mode>_split"
  [(set (match_operand:VSINT_842 0 "altivec_register_operand" "=v")
	(match_operand:VSINT_842 1 "xxspltib_constant_split" "wS"))]
  "TARGET_P9_VECTOR"
  "#"
  "&& 1"
  [(const_int 0)]
{
  int value = 256;
  int num_insns = -1;
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx tmp = ((can_create_pseudo_p ())
	     ? gen_reg_rtx (V16QImode)
	     : gen_lowpart (V16QImode, op0));

  if (!xxspltib_constant_p (op1, <MODE>mode, &num_insns, &value)
      || num_insns != 2)
    gcc_unreachable ();

  emit_insn (gen_xxspltib_v16qi (tmp, GEN_INT (value)));

  if (<MODE>mode == V2DImode)
    emit_insn (gen_vsx_sign_extend_v16qi_v2di (op0, tmp));

  else if (<MODE>mode == V4SImode)
    emit_insn (gen_vsx_sign_extend_v16qi_v4si (op0, tmp));

  else if (<MODE>mode == V8HImode)
    emit_insn (gen_altivec_vupkhsb  (op0, tmp));

  else
    gcc_unreachable ();

  DONE;
}
  [(set_attr "type" "vecperm")
   (set_attr "length" "8")])


;; Prefer using vector registers over GPRs.  Prefer using ISA 3.0's XXSPLTISB
;; or Altivec VSPLITW 0/-1 over XXLXOR/XXLORC to set a register to all 0's or
;; all 1's, since the machine does not have to wait for the previous
;; instruction using the register being set (such as a store waiting on a slow
;; instruction). But generate XXLXOR/XXLORC if it will avoid a register move.

;;              VSX store  VSX load   VSX move  VSX->GPR   GPR->VSX    LQ (GPR)
;;              STQ (GPR)  GPR load   GPR store GPR move   XXSPLTIB    VSPLTISW
;;              LXVKQ      XXSPLTI*
;;              VSX 0/-1   VMX const  GPR const LVX (VMX)  STVX (VMX)
(define_insn "vsx_mov<mode>_64bit"
  [(set (match_operand:VSX_M 0 "nonimmediate_operand"
               "=ZwO,      wa,        wa,        r,         we,        ?wQ,
                ?&r,       ??r,       ??Y,       <??r>,     wa,        v,
                wa,        wa,
                ?wa,       v,         <??r>,     wZ,        v")

	(match_operand:VSX_M 1 "input_operand" 
               "wa,        ZwO,       wa,        we,        r,         r,
                wQ,        Y,         r,         r,         wE,        jwM,
                eQ,        eP,
                ?jwM,      W,         <nW>,      v,         wZ"))]

  "TARGET_POWERPC64 && VECTOR_MEM_VSX_P (<MODE>mode)
   && (register_operand (operands[0], <MODE>mode) 
       || register_operand (operands[1], <MODE>mode))"
{
  return rs6000_output_move_128bit (operands);
}
  [(set_attr "type"
               "vecstore,  vecload,   vecsimple, mtvsr,     mfvsr,     load,
                store,     load,      store,     *,         vecsimple, vecsimple,
                vecperm,   vecperm,
                vecsimple, *,         *,         vecstore,  vecload")
   (set_attr "num_insns"
               "*,         *,         *,         2,         *,         2,
                2,         2,         2,         2,         *,         *,
                *,         *,
                *,         5,         2,         *,         *")
   (set_attr "max_prefixed_insns"
               "*,         *,         *,         *,         *,         2,
                2,         2,         2,         2,         *,         *,
                *,         *,
                *,         *,         *,         *,         *")
   (set_attr "length"
               "*,         *,         *,         8,         *,         8,
                8,         8,         8,         8,         *,         *,
                *,         *,
                *,         20,        8,         *,         *")
   (set_attr "isa"
               "<VSisa>,   <VSisa>,   <VSisa>,   *,         *,         *,
                *,         *,         *,         *,         p9v,       *,
                p10,       p10,
                <VSisa>,   *,         *,         *,         *")
   (set_attr "prefixed"
               "*,         *,         *,         *,         *,         *,
                *,         *,         *,         *,         *,         *,
                *,         yes,
                *,         *,         *,         *,         *")])

;;              VSX store  VSX load   VSX move   GPR load   GPR store  GPR move
;;              LXVKQ      XXSPLTI*
;;              XXSPLTIB   VSPLTISW   VSX 0/-1   VMX const  GPR const
;;              LVX (VMX)  STVX (VMX)
(define_insn "*vsx_mov<mode>_32bit"
  [(set (match_operand:VSX_M 0 "nonimmediate_operand"
               "=ZwO,      wa,        wa,        ??r,       ??Y,       <??r>,
                wa,        wa,
                wa,        v,         ?wa,       v,         <??r>,
                wZ,        v")

	(match_operand:VSX_M 1 "input_operand" 
               "wa,        ZwO,       wa,        Y,         r,         r,
                eQ,        eP,
                wE,        jwM,       ?jwM,      W,         <nW>,
                v,         wZ"))]

  "!TARGET_POWERPC64 && VECTOR_MEM_VSX_P (<MODE>mode)
   && (register_operand (operands[0], <MODE>mode) 
       || register_operand (operands[1], <MODE>mode))"
{
  return rs6000_output_move_128bit (operands);
}
  [(set_attr "type"
               "vecstore,  vecload,   vecsimple, load,      store,    *,
                vecperm,   vecperm,
                vecsimple, vecsimple, vecsimple, *,         *,
                vecstore,  vecload")
   (set_attr "length"
               "*,         *,         *,         16,        16,        16,
                *,         *,
                *,         *,         *,         20,        16,
                *,         *")
   (set_attr "isa"
               "<VSisa>,   <VSisa>,   <VSisa>,   *,         *,         *,
                p10,       p10,
                p9v,       *,         <VSisa>,   *,         *,
                *,         *")
   (set_attr "prefixed"
               "*,         *,         *,         *,         *,         *,
                *,         yes,
                *,         *,         *,         *,         *,
                *,         *")])

;; Explicit  load/store expanders for the builtin functions
(define_expand "vsx_load_<mode>"
  [(set (match_operand:VSX_M 0 "vsx_register_operand")
	(match_operand:VSX_M 1 "memory_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  /* Expand to swaps if needed, prior to swap optimization.  */
  if (!BYTES_BIG_ENDIAN && !TARGET_P9_VECTOR
      && !altivec_indexed_or_indirect_operand(operands[1], <MODE>mode))
    {
      rs6000_emit_le_vsx_move (operands[0], operands[1], <MODE>mode);
      DONE;
    }
})

(define_expand "vsx_store_<mode>"
  [(set (match_operand:VSX_M 0 "memory_operand")
	(match_operand:VSX_M 1 "vsx_register_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  /* Expand to swaps if needed, prior to swap optimization.  */
  if (!BYTES_BIG_ENDIAN && !TARGET_P9_VECTOR
      && !altivec_indexed_or_indirect_operand(operands[0], <MODE>mode))
    {
      rs6000_emit_le_vsx_move (operands[0], operands[1], <MODE>mode);
      DONE;
    }
})

;; Load rightmost element from load_data
;; using lxvrbx, lxvrhx, lxvrwx, lxvrdx.
(define_insn "vsx_lxvr<wd>x"
  [(set (match_operand:TI 0 "vsx_register_operand" "=wa")
	(zero_extend:TI (match_operand:INT_ISA3  1 "memory_operand" "Z")))]
  "TARGET_POWER10"
  "lxvr<wd>x %x0,%y1"
  [(set_attr "type" "vecload")])

;; Store rightmost element into store_data
;; using stxvrbx, stxvrhx, strvxwx, strvxdx.
(define_insn "vsx_stxvr<wd>x"
  [(set (match_operand:INT_ISA3 0 "memory_operand" "=Z")
	(truncate:INT_ISA3 (match_operand:TI 1 "vsx_register_operand" "wa")))]
  "TARGET_POWER10"
  "stxvr<wd>x %x1,%y0"
  [(set_attr "type" "vecstore")])

;; Explicit load/store expanders for the builtin functions for lxvd2x, etc.,
;; when you really want their element-reversing behavior.
(define_insn "vsx_ld_elemrev_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
        (vec_select:V2DI
	  (match_operand:V2DI 1 "memory_operand" "Z")
	  (parallel [(const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V2DImode) && !BYTES_BIG_ENDIAN"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "vsx_ld_elemrev_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=wa")
        (vec_select:V1TI
	  (match_operand:V1TI 1 "memory_operand" "Z")
	  (parallel [(const_int 0)])))]
  "VECTOR_MEM_VSX_P (V1TImode) && !BYTES_BIG_ENDIAN"
{
   return "lxvd2x %x0,%y1\;xxpermdi %x0,%x0,%x0,2";
}
  [(set_attr "type" "vecload")])

(define_insn "vsx_ld_elemrev_v2df"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa")
        (vec_select:V2DF
	  (match_operand:V2DF 1 "memory_operand" "Z")
	  (parallel [(const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V2DFmode) && !BYTES_BIG_ENDIAN"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "vsx_ld_elemrev_v4si"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=wa")
        (vec_select:V4SI
	  (match_operand:V4SI 1 "memory_operand" "Z")
	  (parallel [(const_int 3) (const_int 2)
	             (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V4SImode) && !BYTES_BIG_ENDIAN"
  "lxvw4x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "vsx_ld_elemrev_v4sf"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
        (vec_select:V4SF
	  (match_operand:V4SF 1 "memory_operand" "Z")
	  (parallel [(const_int 3) (const_int 2)
	             (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V4SFmode) && !BYTES_BIG_ENDIAN"
  "lxvw4x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_expand "vsx_ld_elemrev_v8hi"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
        (vec_select:V8HI
	  (match_operand:V8HI 1 "memory_operand" "Z")
	  (parallel [(const_int 7) (const_int 6)
	             (const_int 5) (const_int 4)
		     (const_int 3) (const_int 2)
	             (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V8HImode) && !BYTES_BIG_ENDIAN"
{
  if (!TARGET_P9_VECTOR)
    {
      rtx tmp = gen_reg_rtx (V4SImode);
      rtx subreg, subreg2, perm[16], pcv;
      /* 2 is leftmost element in register */
      unsigned int reorder[16] = {13,12,15,14,9,8,11,10,5,4,7,6,1,0,3,2};
      int i;

      subreg = simplify_gen_subreg (V4SImode, operands[1], V8HImode, 0);
      emit_insn (gen_vsx_ld_elemrev_v4si (tmp, subreg));
      subreg2 = simplify_gen_subreg (V8HImode, tmp, V4SImode, 0);

      for (i = 0; i < 16; ++i)
      	perm[i] = GEN_INT (reorder[i]);

      pcv = force_reg (V16QImode,
                       gen_rtx_CONST_VECTOR (V16QImode,
                                             gen_rtvec_v (16, perm)));
      emit_insn (gen_altivec_vperm_v8hi_direct (operands[0], subreg2,
                                                subreg2, pcv));
      DONE;
    }
})

(define_insn "*vsx_ld_elemrev_v8hi_internal"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
        (vec_select:V8HI
          (match_operand:V8HI 1 "memory_operand" "Z")
          (parallel [(const_int 7) (const_int 6)
                     (const_int 5) (const_int 4)
                     (const_int 3) (const_int 2)
                     (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V8HImode) && !BYTES_BIG_ENDIAN && TARGET_P9_VECTOR"
  "lxvh8x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_expand "vsx_ld_elemrev_v16qi"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
        (vec_select:V16QI
          (match_operand:V16QI 1 "memory_operand" "Z")
          (parallel [(const_int 15) (const_int 14)
                     (const_int 13) (const_int 12)
                     (const_int 11) (const_int 10)
                     (const_int  9) (const_int  8)
                     (const_int  7) (const_int  6)
                     (const_int  5) (const_int  4)
                     (const_int  3) (const_int  2)
                     (const_int  1) (const_int  0)])))]
  "VECTOR_MEM_VSX_P (V16QImode) && !BYTES_BIG_ENDIAN"
{
  if (!TARGET_P9_VECTOR)
    {
      rtx tmp = gen_reg_rtx (V4SImode);
      rtx subreg, subreg2, perm[16], pcv;
      /* 3 is leftmost element in register */
      unsigned int reorder[16] = {12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3};
      int i;

      subreg = simplify_gen_subreg (V4SImode, operands[1], V16QImode, 0);
      emit_insn (gen_vsx_ld_elemrev_v4si (tmp, subreg));
      subreg2 = simplify_gen_subreg (V16QImode, tmp, V4SImode, 0);

      for (i = 0; i < 16; ++i)
        perm[i] = GEN_INT (reorder[i]);

      pcv = force_reg (V16QImode,
                       gen_rtx_CONST_VECTOR (V16QImode,
                                             gen_rtvec_v (16, perm)));
      emit_insn (gen_altivec_vperm_v16qi_direct (operands[0], subreg2,
                                                 subreg2, pcv));
      DONE;
    }
})

(define_insn "vsx_ld_elemrev_v16qi_internal"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
        (vec_select:V16QI
          (match_operand:V16QI 1 "memory_operand" "Z")
          (parallel [(const_int 15) (const_int 14)
                     (const_int 13) (const_int 12)
                     (const_int 11) (const_int 10)
                     (const_int  9) (const_int  8)
                     (const_int  7) (const_int  6)
                     (const_int  5) (const_int  4)
                     (const_int  3) (const_int  2)
                     (const_int  1) (const_int  0)])))]
  "VECTOR_MEM_VSX_P (V16QImode) && !BYTES_BIG_ENDIAN && TARGET_P9_VECTOR"
  "lxvb16x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "vsx_st_elemrev_v1ti"
  [(set (match_operand:V1TI 0 "memory_operand" "=Z")
        (vec_select:V1TI
          (match_operand:V1TI 1 "vsx_register_operand" "+wa")
          (parallel [(const_int 0)])))
   (clobber (match_dup 1))]
  "VECTOR_MEM_VSX_P (V2DImode) && !BYTES_BIG_ENDIAN"
{
  return "xxpermdi %x1,%x1,%x1,2\;stxvd2x %x1,%y0";
}
  [(set_attr "type" "vecstore")])

(define_insn "vsx_st_elemrev_v2df"
  [(set (match_operand:V2DF 0 "memory_operand" "=Z")
        (vec_select:V2DF
          (match_operand:V2DF 1 "vsx_register_operand" "wa")
          (parallel [(const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V2DFmode) && !BYTES_BIG_ENDIAN"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "vsx_st_elemrev_v2di"
  [(set (match_operand:V2DI 0 "memory_operand" "=Z")
        (vec_select:V2DI
          (match_operand:V2DI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V2DImode) && !BYTES_BIG_ENDIAN"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "vsx_st_elemrev_v4sf"
  [(set (match_operand:V4SF 0 "memory_operand" "=Z")
        (vec_select:V4SF
          (match_operand:V4SF 1 "vsx_register_operand" "wa")
          (parallel [(const_int 3) (const_int 2)
                     (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V4SFmode) && !BYTES_BIG_ENDIAN"
  "stxvw4x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "vsx_st_elemrev_v4si"
  [(set (match_operand:V4SI 0 "memory_operand" "=Z")
        (vec_select:V4SI
	  (match_operand:V4SI 1 "vsx_register_operand" "wa")
	  (parallel [(const_int 3) (const_int 2)
	             (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V4SImode) && !BYTES_BIG_ENDIAN"
  "stxvw4x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_expand "vsx_st_elemrev_v8hi"
  [(set (match_operand:V8HI 0 "memory_operand" "=Z")
        (vec_select:V8HI
          (match_operand:V8HI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 7) (const_int 6)
                     (const_int 5) (const_int 4)
                     (const_int 3) (const_int 2)
                     (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V8HImode) && !BYTES_BIG_ENDIAN"
{
  if (!TARGET_P9_VECTOR)
    {
      rtx mem_subreg, subreg, perm[16], pcv;
      rtx tmp = gen_reg_rtx (V8HImode);
      /* 2 is leftmost element in register */
      unsigned int reorder[16] = {13,12,15,14,9,8,11,10,5,4,7,6,1,0,3,2};
      int i;

      for (i = 0; i < 16; ++i)
      	perm[i] = GEN_INT (reorder[i]);

      pcv = force_reg (V16QImode,
                       gen_rtx_CONST_VECTOR (V16QImode,
                                             gen_rtvec_v (16, perm)));
      emit_insn (gen_altivec_vperm_v8hi_direct (tmp, operands[1],
                                                operands[1], pcv));
      subreg = simplify_gen_subreg (V4SImode, tmp, V8HImode, 0);
      mem_subreg = simplify_gen_subreg (V4SImode, operands[0], V8HImode, 0);
      emit_insn (gen_vsx_st_elemrev_v4si (mem_subreg, subreg));
      DONE;
    }
})

(define_insn "*vsx_st_elemrev_v2di_internal"
  [(set (match_operand:V2DI 0 "memory_operand" "=Z")
        (vec_select:V2DI
          (match_operand:V2DI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V2DImode) && !BYTES_BIG_ENDIAN && TARGET_P9_VECTOR"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "*vsx_st_elemrev_v8hi_internal"
  [(set (match_operand:V8HI 0 "memory_operand" "=Z")
        (vec_select:V8HI
          (match_operand:V8HI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 7) (const_int 6)
                     (const_int 5) (const_int 4)
                     (const_int 3) (const_int 2)
                     (const_int 1) (const_int 0)])))]
  "VECTOR_MEM_VSX_P (V8HImode) && !BYTES_BIG_ENDIAN && TARGET_P9_VECTOR"
  "stxvh8x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_expand "vsx_st_elemrev_v16qi"
  [(set (match_operand:V16QI 0 "memory_operand" "=Z")
        (vec_select:V16QI
          (match_operand:V16QI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 15) (const_int 14)
                     (const_int 13) (const_int 12)
                     (const_int 11) (const_int 10)
                     (const_int  9) (const_int  8)
                     (const_int  7) (const_int  6)
                     (const_int  5) (const_int  4)
                     (const_int  3) (const_int  2)
                     (const_int  1) (const_int  0)])))]
  "VECTOR_MEM_VSX_P (V16QImode) && !BYTES_BIG_ENDIAN"
{
  if (!TARGET_P9_VECTOR)
    {
      rtx mem_subreg, subreg, perm[16], pcv;
      rtx tmp = gen_reg_rtx (V16QImode);
      /* 3 is leftmost element in register */
      unsigned int reorder[16] = {12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3};
      int i;

      for (i = 0; i < 16; ++i)
      	perm[i] = GEN_INT (reorder[i]);

      pcv = force_reg (V16QImode,
                       gen_rtx_CONST_VECTOR (V16QImode,
                                             gen_rtvec_v (16, perm)));
      emit_insn (gen_altivec_vperm_v16qi_direct (tmp, operands[1],
                                                 operands[1], pcv));
      subreg = simplify_gen_subreg (V4SImode, tmp, V16QImode, 0);
      mem_subreg = simplify_gen_subreg (V4SImode, operands[0], V16QImode, 0);
      emit_insn (gen_vsx_st_elemrev_v4si (mem_subreg, subreg));
      DONE;
    }
})

(define_insn "*vsx_st_elemrev_v16qi_internal"
  [(set (match_operand:V16QI 0 "memory_operand" "=Z")
        (vec_select:V16QI
          (match_operand:V16QI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 15) (const_int 14)
                     (const_int 13) (const_int 12)
                     (const_int 11) (const_int 10)
                     (const_int  9) (const_int  8)
                     (const_int  7) (const_int  6)
                     (const_int  5) (const_int  4)
                     (const_int  3) (const_int  2)
                     (const_int  1) (const_int  0)])))]
  "VECTOR_MEM_VSX_P (V16QImode) && !BYTES_BIG_ENDIAN && TARGET_P9_VECTOR"
  "stxvb16x %x1,%y0"
  [(set_attr "type" "vecstore")])


;; VSX vector floating point arithmetic instructions.  The VSX scalar
;; instructions are now combined with the insn for the traditional floating
;; point unit.
(define_insn "*vsx_add<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (plus:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		    (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvadd<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_sub<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa>")
        (minus:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		     (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvsub<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_mul<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (mult:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		    (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvmul<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

; Emulate vector with scalar for vec_mul in V2DImode
(define_insn_and_split "vsx_mul_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
        (unspec:V2DI [(match_operand:V2DI 1 "vsx_register_operand" "wa")
                      (match_operand:V2DI 2 "vsx_register_operand" "wa")]
                     UNSPEC_VSX_MULSD))]
  "VECTOR_MEM_VSX_P (V2DImode)"
  "#"
  "VECTOR_MEM_VSX_P (V2DImode) && !reload_completed"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];

  if (TARGET_POWER10)
    emit_insn (gen_mulv2di3 (op0, op1, op2) );

  else
    {
      rtx op3 = gen_reg_rtx (DImode);
      rtx op4 = gen_reg_rtx (DImode);
      rtx op5 = gen_reg_rtx (DImode);
      emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (0)));
      emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (0)));
      if (TARGET_POWERPC64)
	emit_insn (gen_muldi3 (op5, op3, op4));
      else
	{
	  rtx ret = expand_mult (DImode, op3, op4, NULL, 0, false);
	  emit_move_insn (op5, ret);
	}
      emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (1)));
      emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (1)));
      if (TARGET_POWERPC64)
	emit_insn (gen_muldi3 (op3, op3, op4));
      else
	{
	  rtx ret = expand_mult (DImode, op3, op4, NULL, 0, false);
	  emit_move_insn (op3, ret);
	}
      emit_insn (gen_vsx_concat_v2di (op0, op5, op3));
    }
  DONE;
}
  [(set_attr "type" "mul")])

(define_insn "*vsx_div<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (div:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		   (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvdiv<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_div>")])

; Emulate vector with scalar for vec_div in V2DImode
(define_insn_and_split "vsx_div_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
        (unspec:V2DI [(match_operand:V2DI 1 "vsx_register_operand" "wa")
                      (match_operand:V2DI 2 "vsx_register_operand" "wa")]
                     UNSPEC_VSX_DIVSD))]
  "VECTOR_MEM_VSX_P (V2DImode)"
  "#"
  "VECTOR_MEM_VSX_P (V2DImode) && !reload_completed"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op3 = gen_reg_rtx (DImode);
  rtx op4 = gen_reg_rtx (DImode);
  rtx op5 = gen_reg_rtx (DImode);
  emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (0)));
  emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (0)));
  if (TARGET_POWERPC64)
    emit_insn (gen_divdi3 (op5, op3, op4));
  else
    {
      rtx libfunc = optab_libfunc (sdiv_optab, DImode);
      rtx target = emit_library_call_value (libfunc,
					    op5, LCT_NORMAL, DImode,
					    op3, DImode,
					    op4, DImode);
      emit_move_insn (op5, target);
    }
  emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (1)));
  emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (1)));
  if (TARGET_POWERPC64)
    emit_insn (gen_divdi3 (op3, op3, op4));
  else
    {
      rtx libfunc = optab_libfunc (sdiv_optab, DImode);
      rtx target = emit_library_call_value (libfunc,
					    op3, LCT_NORMAL, DImode,
					    op3, DImode,
					    op4, DImode);
      emit_move_insn (op3, target);
    }
  emit_insn (gen_vsx_concat_v2di (op0, op5, op3));
  DONE;
}
  [(set_attr "type" "div")])

(define_insn_and_split "vsx_udiv_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
        (unspec:V2DI [(match_operand:V2DI 1 "vsx_register_operand" "wa")
                      (match_operand:V2DI 2 "vsx_register_operand" "wa")]
                     UNSPEC_VSX_DIVUD))]
  "VECTOR_MEM_VSX_P (V2DImode)"
  "#"
  "VECTOR_MEM_VSX_P (V2DImode) && !reload_completed"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];

    if (TARGET_POWER10)
      emit_insn (gen_udivv2di3 (op0, op1, op2) );
    else
      {
	rtx op3 = gen_reg_rtx (DImode);
	rtx op4 = gen_reg_rtx (DImode);
	rtx op5 = gen_reg_rtx (DImode);

	emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (0)));
	emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (0)));

	if (TARGET_POWERPC64)
	  emit_insn (gen_udivdi3 (op5, op3, op4));
	else
	  {
	    rtx libfunc = optab_libfunc (udiv_optab, DImode);
	    rtx target = emit_library_call_value (libfunc,
						  op5, LCT_NORMAL, DImode,
						  op3, DImode,
						  op4, DImode);
	    emit_move_insn (op5, target);
	  }
	emit_insn (gen_vsx_extract_v2di (op3, op1, GEN_INT (1)));
	emit_insn (gen_vsx_extract_v2di (op4, op2, GEN_INT (1)));

	if (TARGET_POWERPC64)
	  emit_insn (gen_udivdi3 (op3, op3, op4));
	else
	  {
	    rtx libfunc = optab_libfunc (udiv_optab, DImode);
	    rtx target = emit_library_call_value (libfunc,
						  op3, LCT_NORMAL, DImode,
						  op3, DImode,
						  op4, DImode);
	    emit_move_insn (op3, target);
	  }
	emit_insn (gen_vsx_concat_v2di (op0, op5, op3));
      }
    DONE;
}
  [(set_attr "type" "div")])

;; Vector integer signed/unsigned divide
(define_insn "vsx_div_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_DIVSQ))]
  "TARGET_POWER10"
  "vdivsq %0,%1,%2"
  [(set_attr "type" "div")])

(define_insn "vsx_udiv_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_DIVUQ))]
  "TARGET_POWER10"
  "vdivuq %0,%1,%2"
  [(set_attr "type" "div")])

(define_insn "vsx_dives_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_DIVESQ))]
  "TARGET_POWER10"
  "vdivesq %0,%1,%2"
  [(set_attr "type" "div")])

(define_insn "vsx_diveu_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_DIVEUQ))]
  "TARGET_POWER10"
  "vdiveuq %0,%1,%2"
  [(set_attr "type" "div")])

(define_insn "vsx_mods_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_MODSQ))]
  "TARGET_POWER10"
  "vmodsq %0,%1,%2"
  [(set_attr "type" "div")])

(define_insn "vsx_modu_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V1TI 1 "vsx_register_operand" "v")
		      (match_operand:V1TI 2 "vsx_register_operand" "v")]
		     UNSPEC_VSX_MODUQ))]
  "TARGET_POWER10"
  "vmoduq %0,%1,%2"
  [(set_attr "type" "div")])

;; *tdiv* instruction returning the FG flag
(define_expand "vsx_tdiv<mode>3_fg"
  [(set (match_dup 3)
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand")
		      (match_operand:VSX_B 2 "vsx_register_operand")]
		     UNSPEC_VSX_TDIV))
   (set (match_operand:SI 0 "gpc_reg_operand")
	(gt:SI (match_dup 3)
	       (const_int 0)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
{
  operands[3] = gen_reg_rtx (CCFPmode);
})

;; *tdiv* instruction returning the FE flag
(define_expand "vsx_tdiv<mode>3_fe"
  [(set (match_dup 3)
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand")
		      (match_operand:VSX_B 2 "vsx_register_operand")]
		     UNSPEC_VSX_TDIV))
   (set (match_operand:SI 0 "gpc_reg_operand")
	(eq:SI (match_dup 3)
	       (const_int 0)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
{
  operands[3] = gen_reg_rtx (CCFPmode);
})

(define_insn "*vsx_tdiv<mode>3_internal"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=x")
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand" "wa")
		      (match_operand:VSX_B 2 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_TDIV))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>tdiv<sd>p %0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_fre<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
		      UNSPEC_FRES))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvre<sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_neg<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (neg:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvneg<sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_abs<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (abs:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvabs<sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_nabs<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (neg:VSX_F
	 (abs:VSX_F
	  (match_operand:VSX_F 1 "vsx_register_operand" "wa"))))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvnabs<sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_smax<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (smax:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		    (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvmax<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_smin<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (smin:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		    (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvmin<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_sqrt<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
        (sqrt:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvsqrt<sd>p %x0,%x1"
  [(set_attr "type" "<sd>sqrt")])

(define_insn "*vsx_rsqrte<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
		      UNSPEC_RSQRT))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvrsqrte<sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

;; *tsqrt* returning the fg flag
(define_expand "vsx_tsqrt<mode>2_fg"
  [(set (match_dup 2)
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand")]
		     UNSPEC_VSX_TSQRT))
   (set (match_operand:SI 0 "gpc_reg_operand")
	(gt:SI (match_dup 2)
	       (const_int 0)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
{
  operands[2] = gen_reg_rtx (CCFPmode);
})

;; *tsqrt* returning the fe flag
(define_expand "vsx_tsqrt<mode>2_fe"
  [(set (match_dup 2)
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand")]
		     UNSPEC_VSX_TSQRT))
   (set (match_operand:SI 0 "gpc_reg_operand")
	(eq:SI (match_dup 2)
	       (const_int 0)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
{
  operands[2] = gen_reg_rtx (CCFPmode);
})

(define_insn "*vsx_tsqrt<mode>2_internal"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=x")
	(unspec:CCFP [(match_operand:VSX_B 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_TSQRT))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>tsqrt<sd>p %0,%x1"
  [(set_attr "type" "<VStype_simple>")])

;; Fused vector multiply/add instructions. Do not generate the Altivec versions
;; of fma (vmaddfp and vnmsubfp).  These instructions allows the target to be a
;; separate register from the 3 inputs, but they have different rounding
;; behaviors than the VSX instructions.
(define_insn "*vsx_fmav4sf4"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa,wa")
	(fma:V4SF
	  (match_operand:V4SF 1 "vsx_register_operand" "%wa,wa")
	  (match_operand:V4SF 2 "vsx_register_operand" "wa,0")
	  (match_operand:V4SF 3 "vsx_register_operand" "0,wa")))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "@
   xvmaddasp %x0,%x1,%x2
   xvmaddmsp %x0,%x1,%x3"
  [(set_attr "type" "vecfloat")])

(define_insn "*vsx_fmav2df4"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa,wa")
	(fma:V2DF
	  (match_operand:V2DF 1 "vsx_register_operand" "%wa,wa")
	  (match_operand:V2DF 2 "vsx_register_operand" "wa,0")
	  (match_operand:V2DF 3 "vsx_register_operand" "0,wa")))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "@
   xvmaddadp %x0,%x1,%x2
   xvmaddmdp %x0,%x1,%x3"
  [(set_attr "type" "vecdouble")])

(define_insn "*vsx_fms<mode>4"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa,wa")
	(fma:VSX_F
	  (match_operand:VSX_F 1 "vsx_register_operand" "%wa,wa")
	  (match_operand:VSX_F 2 "vsx_register_operand" "wa,0")
	  (neg:VSX_F
	    (match_operand:VSX_F 3 "vsx_register_operand" "0,wa"))))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "@
   xvmsuba<sd>p %x0,%x1,%x2
   xvmsubm<sd>p %x0,%x1,%x3"
  [(set_attr "type" "<VStype_mul>")])

(define_insn "*vsx_nfma<mode>4"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa,wa")
	(neg:VSX_F
	 (fma:VSX_F
	  (match_operand:VSX_F 1 "vsx_register_operand" "wa,wa")
	  (match_operand:VSX_F 2 "vsx_register_operand" "wa,0")
	  (match_operand:VSX_F 3 "vsx_register_operand" "0,wa"))))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "@
   xvnmadda<sd>p %x0,%x1,%x2
   xvnmaddm<sd>p %x0,%x1,%x3"
  [(set_attr "type" "<VStype_mul>")])

(define_insn "*vsx_nfmsv4sf4"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa,wa")
	(neg:V4SF
	 (fma:V4SF
	   (match_operand:V4SF 1 "vsx_register_operand" "%wa,wa")
	   (match_operand:V4SF 2 "vsx_register_operand" "wa,0")
	   (neg:V4SF
	     (match_operand:V4SF 3 "vsx_register_operand" "0,wa")))))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "@
   xvnmsubasp %x0,%x1,%x2
   xvnmsubmsp %x0,%x1,%x3"
  [(set_attr "type" "vecfloat")])

(define_insn "*vsx_nfmsv2df4"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa,wa")
	(neg:V2DF
	 (fma:V2DF
	   (match_operand:V2DF 1 "vsx_register_operand" "%wa,wa")
	   (match_operand:V2DF 2 "vsx_register_operand" "wa,0")
	   (neg:V2DF
	     (match_operand:V2DF 3 "vsx_register_operand" "0,wa")))))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "@
   xvnmsubadp %x0,%x1,%x2
   xvnmsubmdp %x0,%x1,%x3"
  [(set_attr "type" "vecdouble")])

;; Vector conditional expressions (no scalar version for these instructions)
(define_insn "vsx_eq<mode>"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(eq:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		  (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpeq<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_gt<mode>"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(gt:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		  (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpgt<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_ge<mode>"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(ge:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		  (match_operand:VSX_F 2 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpge<sd>p %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

;; Compare vectors producing a vector result and a predicate, setting CR6 to
;; indicate a combined status
(define_insn "*vsx_eq_<mode>_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC
	 [(eq:CC (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		 (match_operand:VSX_F 2 "vsx_register_operand" "wa"))]
	 UNSPEC_PREDICATE))
   (set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(eq:VSX_F (match_dup 1)
		  (match_dup 2)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpeq<sd>p. %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_gt_<mode>_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC
	 [(gt:CC (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		 (match_operand:VSX_F 2 "vsx_register_operand" "wa"))]
	 UNSPEC_PREDICATE))
   (set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(gt:VSX_F (match_dup 1)
		  (match_dup 2)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpgt<sd>p. %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

;; xvtlsbb BF,XB
;; Set the CR field BF to indicate if the lowest bit (bit 7) of every byte
;; element in VSR[XB] is equal to 1 (ALL_TRUE) or equal to 0 (ALL_FALSE).
(define_insn "*xvtlsbb_internal"
  [(set (match_operand:CC 0 "cc_reg_operand" "=y")
	(unspec:CC [(match_operand:V16QI 1 "vsx_register_operand" "wa")]
	 UNSPEC_XVTLSBB))]
  "TARGET_POWER10"
  "xvtlsbb %0,%x1"
  [(set_attr "type" "logical")])

;; Vector Test Least Significant Bit by Byte
;; for the implementation of the builtin
;;     __builtin_vec_test_lsbb_all_ones
;;     int vec_test_lsbb_all_ones (vector unsigned char);
;; and
;;     __builtin_vec_test_lsbb_all_zeros
;;     int vec_test_lsbb_all_zeros (vector unsigned char);
(define_expand "xvtlsbbo"
  [(set (match_dup 2)
	(unspec:CC [(match_operand:V16QI 1 "vsx_register_operand" "v")]
	 UNSPEC_XVTLSBB))
   (set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(lt:SI (match_dup 2) (const_int 0)))]
  "TARGET_POWER10"
{
   operands[2] = gen_reg_rtx (CCmode);
})
(define_expand "xvtlsbbz"
  [(set (match_dup 2)
	(unspec:CC [(match_operand:V16QI 1 "vsx_register_operand" "v")]
	 UNSPEC_XVTLSBB))
   (set (match_operand:SI 0 "gpc_reg_operand" "=r")
	(eq:SI (match_dup 2) (const_int 0)))]
  "TARGET_POWER10"
{
   operands[2] = gen_reg_rtx (CCmode);
})

(define_insn "*vsx_ge_<mode>_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC
	 [(ge:CC (match_operand:VSX_F 1 "vsx_register_operand" "wa")
		 (match_operand:VSX_F 2 "vsx_register_operand" "wa"))]
	 UNSPEC_PREDICATE))
   (set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(ge:VSX_F (match_dup 1)
		  (match_dup 2)))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcmpge<sd>p. %x0,%x1,%x2"
  [(set_attr "type" "<VStype_simple>")])

;; Copy sign
(define_insn "vsx_copysign<mode>3"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F
	 [(match_operand:VSX_F 1 "vsx_register_operand" "wa")
	  (match_operand:VSX_F 2 "vsx_register_operand" "wa")]
	 UNSPEC_COPYSIGN))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcpsgn<sd>p %x0,%x2,%x1"
  [(set_attr "type" "<VStype_simple>")])

;; For the conversions, limit the register class for the integer value to be
;; the fprs because we don't want to add the altivec registers to movdi/movsi.
;; For the unsigned tests, there isn't a generic double -> unsigned conversion
;; in rs6000.md so don't test VECTOR_UNIT_VSX_P, just test against VSX.
;; Don't use vsx_register_operand here, use gpc_reg_operand to match rs6000.md
;; in allowing virtual registers.
(define_insn "vsx_float<VSi><mode>2"
  [(set (match_operand:VSX_F 0 "gpc_reg_operand" "=wa")
	(float:VSX_F (match_operand:<VSI> 1 "gpc_reg_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcvsx<VSc><sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_floatuns<VSi><mode>2"
  [(set (match_operand:VSX_F 0 "gpc_reg_operand" "=wa")
	(unsigned_float:VSX_F (match_operand:<VSI> 1 "gpc_reg_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvcvux<VSc><sd>p %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_fix_trunc<mode><VSi>2"
  [(set (match_operand:<VSI> 0 "gpc_reg_operand" "=wa")
	(fix:<VSI> (match_operand:VSX_F 1 "gpc_reg_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>cv<sd>psx<VSc>s %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_fixuns_trunc<mode><VSi>2"
  [(set (match_operand:<VSI> 0 "gpc_reg_operand" "=wa")
	(unsigned_fix:<VSI> (match_operand:VSX_F 1 "gpc_reg_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>cv<sd>pux<VSc>s %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

;; Math rounding functions
(define_insn "vsx_x<VSv>r<sd>pi"
  [(set (match_operand:VSX_B 0 "vsx_register_operand" "=wa")
	(unspec:VSX_B [(match_operand:VSX_B 1 "vsx_register_operand" "wa")]
		      UNSPEC_VSX_ROUND_I))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>r<sd>pi %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_x<VSv>r<sd>pic"
  [(set (match_operand:VSX_B 0 "vsx_register_operand" "=wa")
	(unspec:VSX_B [(match_operand:VSX_B 1 "vsx_register_operand" "wa")]
		      UNSPEC_VSX_ROUND_IC))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>r<sd>pic %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_btrunc<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(fix:VSX_F (match_operand:VSX_F 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvr<sd>piz %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "*vsx_b2trunc<mode>2"
  [(set (match_operand:VSX_B 0 "vsx_register_operand" "=wa")
	(unspec:VSX_B [(match_operand:VSX_B 1 "vsx_register_operand" "wa")]
		      UNSPEC_FRIZ))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "x<VSv>r<sd>piz %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_floor<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
		      UNSPEC_FRIM))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvr<sd>pim %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])

(define_insn "vsx_ceil<mode>2"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
		      UNSPEC_FRIP))]
  "VECTOR_UNIT_VSX_P (<MODE>mode)"
  "xvr<sd>pip %x0,%x1"
  [(set_attr "type" "<VStype_simple>")])


;; VSX convert to/from double vector

;; Convert between single and double precision
;; Don't use xscvspdp and xscvdpsp for scalar conversions, since the normal
;; scalar single precision instructions internally use the double format.
;; Prefer the altivec registers, since we likely will need to do a vperm
(define_insn "vsx_xscvdpsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=f,?wa")
	(unspec:V4SF [(match_operand:DF 1 "vsx_register_operand" "f,wa")]
			      UNSPEC_VSX_CVSPDP))]
  "VECTOR_UNIT_VSX_P (DFmode)"
  "xscvdpsp %x0,%x1"
  [(set_attr "type" "fp")])

(define_insn "vsx_xvcvspdp_be"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=v,?wa")
     (float_extend:V2DF
       (vec_select:V2SF (match_operand:V4SF 1 "vsx_register_operand" "wa,wa")
	 (parallel [(const_int 0) (const_int 2)]))))]
  "VECTOR_UNIT_VSX_P (V4SFmode) && BYTES_BIG_ENDIAN"
  "xvcvspdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvspdp_le"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=v,?wa")
     (float_extend:V2DF
       (vec_select:V2SF (match_operand:V4SF 1 "vsx_register_operand" "wa,wa")
	 (parallel [(const_int 1) (const_int 3)]))))]
  "VECTOR_UNIT_VSX_P (V4SFmode) && !BYTES_BIG_ENDIAN"
  "xvcvspdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_expand "vsx_xvcvspdp"
  [(match_operand:V2DF 0 "vsx_register_operand")
   (match_operand:V4SF 1 "vsx_register_operand")]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vsx_xvcvspdp_be (operands[0], operands[1]));
  else
    emit_insn (gen_vsx_xvcvspdp_le (operands[0], operands[1]));
  DONE;
})

(define_insn "vsx_xvcvdpsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa,?wa")
	(unspec:V4SF [(match_operand:V2DF 1 "vsx_register_operand" "v,wa")]
			      UNSPEC_VSX_CVSPDP))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvdpsp %x0,%x1"
  [(set_attr "type" "vecdouble")])

;; xscvspdp, represent the scalar SF type as V4SF
(define_insn "vsx_xscvspdp"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:V4SF 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVSPDP))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "xscvspdp %x0,%x1"
  [(set_attr "type" "fp")])

;; Same as vsx_xscvspdp, but use SF as the type
(define_insn "vsx_xscvspdp_scalar2"
  [(set (match_operand:SF 0 "vsx_register_operand" "=wa")
	(unspec:SF [(match_operand:V4SF 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVSPDP))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "xscvspdp %x0,%x1"
  [(set_attr "type" "fp")])

;; Generate xvcvhpsp instruction
(define_insn "vsx_xvcvhpsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand: V16QI 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVHPSP))]
  "TARGET_P9_VECTOR"
  "xvcvhpsp %x0,%x1"
  [(set_attr "type" "vecfloat")])

;; Generate xvcvsphp
(define_insn "vsx_xvcvsphp"
  [(set (match_operand:V4SI 0 "register_operand" "=wa")
	(unspec:V4SI [(match_operand:V4SF 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_XVCVSPHP))]
  "TARGET_P9_VECTOR"
  "xvcvsphp %x0,%x1"
[(set_attr "type" "vecfloat")])

;; xscvdpsp used for splat'ing a scalar to V4SF, knowing that the internal SF
;; format of scalars is actually DF.
(define_insn "vsx_xscvdpsp_scalar"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand:SF 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVSPDP))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "xscvdpsp %x0,%x1"
  [(set_attr "type" "fp")])

;; ISA 2.07 xscvdpspn/xscvspdpn that does not raise an error on signalling NaNs
(define_insn "vsx_xscvdpspn"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand:DF 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVDPSPN))]
  "TARGET_XSCVDPSPN"
  "xscvdpspn %x0,%x1"
  [(set_attr "type" "fp")])

(define_insn "vsx_xscvspdpn"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:V4SF 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVSPDPN))]
  "TARGET_XSCVSPDPN"
  "xscvspdpn %x0,%x1"
  [(set_attr "type" "fp")])

(define_insn "vsx_xscvdpspn_scalar"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand:SF 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVDPSPN))]
  "TARGET_XSCVDPSPN"
  "xscvdpspn %x0,%x1"
  [(set_attr "type" "fp")])

;; Used by direct move to move a SFmode value from GPR to VSX register
(define_insn "vsx_xscvspdpn_directmove"
  [(set (match_operand:SF 0 "vsx_register_operand" "=wa")
	(unspec:SF [(match_operand:SF 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVSPDPN))]
  "TARGET_XSCVSPDPN"
  "xscvspdpn %x0,%x1"
  [(set_attr "type" "fp")])

;; Convert and scale (used by vec_ctf, vec_cts, vec_ctu for double/long long)

(define_insn "vsx_xvcv<su>xwsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
     (any_float:V4SF (match_operand:V4SI 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "xvcv<su>xwsp %x0,%x1"
  [(set_attr "type" "vecfloat")])

(define_insn "vsx_xvcv<su>xddp"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa")
        (any_float:V2DF (match_operand:V2DI 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcv<su>xddp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvsp<su>xws"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=wa")
        (any_fix:V4SI (match_operand:V4SF 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "xvcvsp<su>xws %x0,%x1"
  [(set_attr "type" "vecfloat")])

(define_insn "vsx_xvcvdp<su>xds"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
        (any_fix:V2DI (match_operand:V2DF 1 "vsx_register_operand" "wa")))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvdp<su>xds %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_expand "vsx_xvcvsxddp_scale"
  [(match_operand:V2DF 0 "vsx_register_operand")
   (match_operand:V2DI 1 "vsx_register_operand")
   (match_operand:QI 2 "immediate_operand")]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  int scale = INTVAL(operands[2]);
  emit_insn (gen_vsx_xvcvsxddp (op0, op1));
  if (scale != 0)
    rs6000_scale_v2df (op0, op0, -scale);
  DONE;
})

(define_expand "vsx_xvcvuxddp_scale"
  [(match_operand:V2DF 0 "vsx_register_operand")
   (match_operand:V2DI 1 "vsx_register_operand")
   (match_operand:QI 2 "immediate_operand")]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  int scale = INTVAL(operands[2]);
  emit_insn (gen_vsx_xvcvuxddp (op0, op1));
  if (scale != 0)
    rs6000_scale_v2df (op0, op0, -scale);
  DONE;
})

(define_expand "vsx_xvcvdpsxds_scale"
  [(match_operand:V2DI 0 "vsx_register_operand")
   (match_operand:V2DF 1 "vsx_register_operand")
   (match_operand:QI 2 "immediate_operand")]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx tmp;
  int scale = INTVAL (operands[2]);
  if (scale == 0)
    tmp = op1;
  else
    {
      tmp  = gen_reg_rtx (V2DFmode);
      rs6000_scale_v2df (tmp, op1, scale);
    }
  emit_insn (gen_vsx_xvcvdpsxds (op0, tmp));
  DONE;
})

;; convert vector of 64-bit floating point numbers to vector of
;; 64-bit unsigned integer
(define_expand "vsx_xvcvdpuxds_scale"
  [(match_operand:V2DI 0 "vsx_register_operand")
   (match_operand:V2DF 1 "vsx_register_operand")
   (match_operand:QI 2 "immediate_operand")]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx tmp;
  int scale = INTVAL (operands[2]);
  if (scale == 0)
    tmp = op1;
  else
    {
      tmp = gen_reg_rtx (V2DFmode);
      rs6000_scale_v2df (tmp, op1, scale);
    }
  emit_insn (gen_vsx_xvcvdpuxds (op0, tmp));
  DONE;
})

;; Convert from 64-bit to 32-bit types
;; Note, favor the Altivec registers since the usual use of these instructions
;; is in vector converts and we need to use the Altivec vperm instruction.

(define_insn "vsx_xvcvdpsxws"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=v,?wa")
	(unspec:V4SI [(match_operand:V2DF 1 "vsx_register_operand" "wa,wa")]
		     UNSPEC_VSX_CVDPSXWS))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvdpsxws %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvdpuxws"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=v,?wa")
	(unspec:V4SI [(match_operand:V2DF 1 "vsx_register_operand" "wa,wa")]
		     UNSPEC_VSX_CVDPUXWS))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvdpuxws %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvsxdsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand:V2DI 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVSXDSP))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvsxdsp %x0,%x1"
  [(set_attr "type" "vecfloat")])

(define_insn "vsx_xvcvuxdsp"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa")
	(unspec:V4SF [(match_operand:V2DI 1 "vsx_register_operand" "wa")]
		     UNSPEC_VSX_CVUXDSP))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "xvcvuxdsp %x0,%x1"
  [(set_attr "type" "vecdouble")])

;; Convert vector of 32-bit signed/unsigned integers to vector of
;; 64-bit floating point numbers.
(define_insn "vsx_xvcv<su>xwdp_be"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa")
     (any_float:V2DF
       (vec_select:V2SI (match_operand:V4SI 1 "vsx_register_operand" "wa")
	 (parallel [(const_int 0) (const_int 2)]))))]
  "VECTOR_UNIT_VSX_P (V2DFmode) && BYTES_BIG_ENDIAN"
  "xvcv<su>xwdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcv<su>xwdp_le"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa")
     (any_float:V2DF
       (vec_select:V2SI (match_operand:V4SI 1 "vsx_register_operand" "wa")
	 (parallel [(const_int 1) (const_int 3)]))))]
  "VECTOR_UNIT_VSX_P (V2DFmode) && !BYTES_BIG_ENDIAN"
  "xvcv<su>xwdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_expand "vsx_xvcv<su>xwdp"
  [(match_operand:V2DF 0 "vsx_register_operand")
   (match_operand:V4SI 1 "vsx_register_operand")
   (any_float (pc))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vsx_xvcv<su>xwdp_be (operands[0], operands[1]));
  else
    emit_insn (gen_vsx_xvcv<su>xwdp_le (operands[0], operands[1]));
  DONE;
})

(define_insn "vsx_xvcvsxwdp_df"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:V4SI 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVSXWDP))]
  "TARGET_VSX"
  "xvcvsxwdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvuxwdp_df"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:V4SI 1 "vsx_register_operand" "wa")]
		   UNSPEC_VSX_CVUXWDP))]
  "TARGET_VSX"
  "xvcvuxwdp %x0,%x1"
  [(set_attr "type" "vecdouble")])

;; Convert vector of 32-bit floating point numbers to vector of
;; 64-bit signed/unsigned integers.
(define_insn "vsx_xvcvsp<su>xds_be"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=v,?wa")
     (any_fix:V2DI
       (vec_select:V2SF (match_operand:V4SF 1 "vsx_register_operand" "wa,wa")
	 (parallel [(const_int 0) (const_int 2)]))))]
  "VECTOR_UNIT_VSX_P (V2DFmode) && BYTES_BIG_ENDIAN"
  "xvcvsp<su>xds %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_insn "vsx_xvcvsp<su>xds_le"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=v,?wa")
     (any_fix:V2DI
       (vec_select:V2SF (match_operand:V4SF 1 "vsx_register_operand" "wa,wa")
	 (parallel [(const_int 1) (const_int 3)]))))]
  "VECTOR_UNIT_VSX_P (V2DFmode) && !BYTES_BIG_ENDIAN"
  "xvcvsp<su>xds %x0,%x1"
  [(set_attr "type" "vecdouble")])

(define_expand "vsx_xvcvsp<su>xds"
  [(match_operand:V2DI 0 "vsx_register_operand")
   (match_operand:V4SF 1 "vsx_register_operand")
   (any_fix (pc))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vsx_xvcvsp<su>xds_be (operands[0], operands[1]));
  else
    emit_insn (gen_vsx_xvcvsp<su>xds_le (operands[0], operands[1]));
  DONE;
})

;; Generate float2 double
;; convert two double to float
(define_expand "float2_v2df"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:V2DF 1 "register_operand" "wa"))
   (use (match_operand:V2DF 2 "register_operand" "wa"))]
 "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  rtx rtx_src1, rtx_src2, rtx_dst;

  rtx_dst = operands[0];
  rtx_src1 = operands[1];
  rtx_src2 = operands[2];

  rs6000_generate_float2_double_code (rtx_dst, rtx_src1, rtx_src2);
  DONE;
})

;; Generate float2
;; convert two long long signed ints to float
(define_expand "float2_v2di"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:V2DI 1 "register_operand" "wa"))
   (use (match_operand:V2DI 2 "register_operand" "wa"))]
 "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  rtx rtx_src1, rtx_src2, rtx_dst;

  rtx_dst = operands[0];
  rtx_src1 = operands[1];
  rtx_src2 = operands[2];

  rs6000_generate_float2_code (true, rtx_dst, rtx_src1, rtx_src2);
  DONE;
})

;; Generate uns_float2
;; convert two long long unsigned ints to float
(define_expand "uns_float2_v2di"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:V2DI 1 "register_operand" "wa"))
   (use (match_operand:V2DI 2 "register_operand" "wa"))]
 "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  rtx rtx_src1, rtx_src2, rtx_dst;

  rtx_dst = operands[0];
  rtx_src1 = operands[1];
  rtx_src2 = operands[2];

  rs6000_generate_float2_code (true, rtx_dst, rtx_src1, rtx_src2);
  DONE;
})

;; Generate floate
;; convert  double or long long signed to float
;; (Only even words are valid, BE numbering)
(define_expand "floate<mode>"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:VSX_D 1 "register_operand" "wa"))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  if (BYTES_BIG_ENDIAN)
    {
      /* Shift left one word to put even word correct location */
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (4);

      rtx_tmp = gen_reg_rtx (V4SFmode);
      emit_insn (gen_vsx_xvcv<VF_sxddp>sp (rtx_tmp, operands[1]));
      emit_insn (gen_altivec_vsldoi_v4sf (operands[0],
		 rtx_tmp, rtx_tmp, rtx_val));
    }
  else
    emit_insn (gen_vsx_xvcv<VF_sxddp>sp (operands[0], operands[1]));

  DONE;
})

;; Generate uns_floate
;; convert long long unsigned to float
;; (Only even words are valid, BE numbering)
(define_expand "unsfloatev2di"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:V2DI 1 "register_operand" "wa"))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  if (BYTES_BIG_ENDIAN)
    {
      /* Shift left one word to put even word correct location */
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (4);

      rtx_tmp = gen_reg_rtx (V4SFmode);
      emit_insn (gen_vsx_xvcvuxdsp (rtx_tmp, operands[1]));
      emit_insn (gen_altivec_vsldoi_v4sf (operands[0],
		 rtx_tmp, rtx_tmp, rtx_val));
    }
  else
    emit_insn (gen_vsx_xvcvuxdsp (operands[0], operands[1]));

  DONE;
})

;; Generate floato
;; convert double or long long signed to float
;; Only odd words are valid, BE numbering)
(define_expand "floato<mode>"
  [(use (match_operand:V4SF 0 "register_operand" "=wa"))
   (use (match_operand:VSX_D 1 "register_operand" "wa"))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vsx_xvcv<VF_sxddp>sp (operands[0], operands[1]));
  else
    {
      /* Shift left one word to put odd word correct location */
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (4);

      rtx_tmp = gen_reg_rtx (V4SFmode);
      emit_insn (gen_vsx_xvcv<VF_sxddp>sp (rtx_tmp, operands[1]));
      emit_insn (gen_altivec_vsldoi_v4sf (operands[0],
		 rtx_tmp, rtx_tmp, rtx_val));
    }
  DONE;
})

;; Generate uns_floato
;; convert long long unsigned to float
;; (Only odd words are valid, BE numbering)
(define_expand "unsfloatov2di"
 [(use (match_operand:V4SF 0 "register_operand" "=wa"))
  (use (match_operand:V2DI 1 "register_operand" "wa"))]
 "VECTOR_UNIT_VSX_P (V4SFmode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vsx_xvcvuxdsp (operands[0], operands[1]));
  else
    {
      /* Shift left one word to put odd word correct location */
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (4);

      rtx_tmp = gen_reg_rtx (V4SFmode);
      emit_insn (gen_vsx_xvcvuxdsp (rtx_tmp, operands[1]));
      emit_insn (gen_altivec_vsldoi_v4sf (operands[0],
		 rtx_tmp, rtx_tmp, rtx_val));
    }
  DONE;
})

;; Generate vsigned2
;; convert two double float vectors to a vector of single precision ints
(define_expand "vsigned2_v2df"
  [(match_operand:V4SI 0 "register_operand" "=wa")
   (unspec:V4SI [(match_operand:V2DF 1 "register_operand" "wa")
		 (match_operand:V2DF 2 "register_operand" "wa")]
  UNSPEC_VSX_VSIGNED2)]
  "TARGET_VSX"
{
  rtx rtx_src1, rtx_src2, rtx_dst;
  bool signed_convert=true;

  rtx_dst = operands[0];
  rtx_src1 = operands[1];
  rtx_src2 = operands[2];

  rs6000_generate_vsigned2_code (signed_convert, rtx_dst, rtx_src1, rtx_src2);
  DONE;
})

;; Generate vsignedo_v2df
;; signed double float to int convert odd word
(define_expand "vsignedo_v2df"
  [(set (match_operand:V4SI 0 "register_operand" "=wa")
	(match_operand:V2DF 1 "register_operand" "wa"))]
  "TARGET_VSX"
{
  if (BYTES_BIG_ENDIAN)
    {
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (12);
      rtx_tmp = gen_reg_rtx (V4SImode);

      emit_insn (gen_vsx_xvcvdpsxws (rtx_tmp, operands[1]));

      /* Big endian word numbering for words in operand is 0 1 2 3.
	 take (operand[1] operand[1]) and shift left one word
	 0 1 2 3    0 1 2 3  =>  1 2 3 0
	 Words 1 and 3 are now are now where they need to be for result.  */

      emit_insn (gen_altivec_vsldoi_v4si (operands[0], rtx_tmp,
		 rtx_tmp, rtx_val));
    }
  else
    /* Little endian word numbering for operand is 3 2 1 0.
       Result words 3 and 1 are where they need to be.  */
    emit_insn (gen_vsx_xvcvdpsxws (operands[0], operands[1]));

  DONE;
}
  [(set_attr "type" "veccomplex")])

;; Generate vsignede_v2df
;; signed double float to int even word
(define_expand "vsignede_v2df"
  [(set (match_operand:V4SI 0 "register_operand" "=v")
	(match_operand:V2DF 1 "register_operand" "v"))]
  "TARGET_VSX"
{
  if (BYTES_BIG_ENDIAN)
    /* Big endian word numbering for words in operand is 0 1
       Result words 0 is where they need to be.  */
    emit_insn (gen_vsx_xvcvdpsxws (operands[0], operands[1]));

  else
    {
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (12);
      rtx_tmp = gen_reg_rtx (V4SImode);

      emit_insn (gen_vsx_xvcvdpsxws (rtx_tmp, operands[1]));

      /* Little endian word numbering for operand is 3 2 1 0.
	 take (operand[1] operand[1]) and shift left three words
	 0 1 2 3   0 1 2 3  =>  3 0 1 2
	 Words 0 and 2 are now where they need to be for the result.  */
      emit_insn (gen_altivec_vsldoi_v4si (operands[0], rtx_tmp,
		 rtx_tmp, rtx_val));
    }
  DONE;
}
  [(set_attr "type" "veccomplex")])

;; Generate unsigned2
;; convert two double float vectors to a vector of single precision
;; unsigned ints
(define_expand "vunsigned2_v2df"
[(match_operand:V4SI 0 "register_operand" "=v")
 (unspec:V4SI [(match_operand:V2DF 1 "register_operand" "v")
	       (match_operand:V2DF 2 "register_operand" "v")]
	      UNSPEC_VSX_VSIGNED2)]
 "TARGET_VSX"
{
  rtx rtx_src1, rtx_src2, rtx_dst;
  bool signed_convert=false;

  rtx_dst = operands[0];
  rtx_src1 = operands[1];
  rtx_src2 = operands[2];

  rs6000_generate_vsigned2_code (signed_convert, rtx_dst, rtx_src1, rtx_src2);
  DONE;
})

;; Generate vunsignedo_v2df
;; unsigned double float to int convert odd word
(define_expand "vunsignedo_v2df"
  [(set (match_operand:V4SI 0 "register_operand" "=v")
	(match_operand:V2DF 1 "register_operand" "v"))]
  "TARGET_VSX"
{
  if (BYTES_BIG_ENDIAN)
    {
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (12);
      rtx_tmp = gen_reg_rtx (V4SImode);

      emit_insn (gen_vsx_xvcvdpuxws (rtx_tmp, operands[1]));

      /* Big endian word numbering for words in operand is 0 1 2 3.
	 take (operand[1] operand[1]) and shift left one word
	 0 1 2 3    0 1 2 3  =>  1 2 3 0
	 Words 1 and 3 are now are now where they need to be for result.  */

      emit_insn (gen_altivec_vsldoi_v4si (operands[0], rtx_tmp,
		 rtx_tmp, rtx_val));
    }
  else
    /* Little endian word numbering for operand is 3 2 1 0.
       Result words 3 and 1 are where they need to be.  */
    emit_insn (gen_vsx_xvcvdpuxws (operands[0], operands[1]));

  DONE;
}
  [(set_attr "type" "veccomplex")])

;; Generate vunsignede_v2df
;; unsigned double float to int even word
(define_expand "vunsignede_v2df"
  [(set (match_operand:V4SI 0 "register_operand" "=v")
	(match_operand:V2DF 1 "register_operand" "v"))]
  "TARGET_VSX"
{
  if (BYTES_BIG_ENDIAN)
    /* Big endian word numbering for words in operand is 0 1
       Result words 0 is where they need to be.  */
    emit_insn (gen_vsx_xvcvdpuxws (operands[0], operands[1]));

  else
    {
      rtx rtx_tmp;
      rtx rtx_val = GEN_INT (12);
      rtx_tmp = gen_reg_rtx (V4SImode);

      emit_insn (gen_vsx_xvcvdpuxws (rtx_tmp, operands[1]));

      /* Little endian word numbering for operand is 3 2 1 0.
	 take (operand[1] operand[1]) and shift left three words
	 0 1 2 3   0 1 2 3  =>  3 0 1 2
	 Words 0 and 2 are now where they need to be for the result.  */
      emit_insn (gen_altivec_vsldoi_v4si (operands[0], rtx_tmp,
		 rtx_tmp, rtx_val));
    }
  DONE;
}
  [(set_attr "type" "veccomplex")])

;; Only optimize (float (fix x)) -> frz if we are in fast-math mode, since
;; since the xvrdpiz instruction does not truncate the value if the floating
;; point value is < LONG_MIN or > LONG_MAX.
(define_insn "*vsx_float_fix_v2df2"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa,?wa")
	(float:V2DF
	 (fix:V2DI
	  (match_operand:V2DF 1 "vsx_register_operand" "wa,?wa"))))]
  "TARGET_HARD_FLOAT
   && VECTOR_UNIT_VSX_P (V2DFmode) && flag_unsafe_math_optimizations
   && !flag_trapping_math && TARGET_FRIZ"
  "xvrdpiz %x0,%x1"
  [(set_attr "type" "vecdouble")])


;; Permute operations

;; Build a V2DF/V2DI vector from two scalars
(define_insn "vsx_concat_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa,we")
	(vec_concat:VSX_D
	 (match_operand:<VS_scalar> 1 "gpc_reg_operand" "wa,b")
	 (match_operand:<VS_scalar> 2 "gpc_reg_operand" "wa,b")))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  if (which_alternative == 0)
    return (BYTES_BIG_ENDIAN
	    ? "xxpermdi %x0,%x1,%x2,0"
	    : "xxpermdi %x0,%x2,%x1,0");

  else if (which_alternative == 1)
    return (BYTES_BIG_ENDIAN
	    ? "mtvsrdd %x0,%1,%2"
	    : "mtvsrdd %x0,%2,%1");

  else
    gcc_unreachable ();
}
  [(set_attr "type" "vecperm,vecmove")])

;; Combiner patterns to allow creating XXPERMDI's to access either double
;; word element in a vector register.
(define_insn "*vsx_concat_<mode>_1"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_concat:VSX_D
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_D 1 "gpc_reg_operand" "wa")
	  (parallel [(match_operand:QI 2 "const_0_to_1_operand" "n")]))
	 (match_operand:<VS_scalar> 3 "gpc_reg_operand" "wa")))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  HOST_WIDE_INT dword = INTVAL (operands[2]);
  if (BYTES_BIG_ENDIAN)
    {
      operands[4] = GEN_INT (2*dword);
      return "xxpermdi %x0,%x1,%x3,%4";
    }
  else
    {
      operands[4] = GEN_INT (!dword);
      return "xxpermdi %x0,%x3,%x1,%4";
    }
}
  [(set_attr "type" "vecperm")])

(define_insn "*vsx_concat_<mode>_2"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_concat:VSX_D
	 (match_operand:<VS_scalar> 1 "gpc_reg_operand" "wa")
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_D 2 "gpc_reg_operand" "wa")
	  (parallel [(match_operand:QI 3 "const_0_to_1_operand" "n")]))))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  HOST_WIDE_INT dword = INTVAL (operands[3]);
  if (BYTES_BIG_ENDIAN)
    {
      operands[4] = GEN_INT (dword);
      return "xxpermdi %x0,%x1,%x2,%4";
    }
  else
    {
      operands[4] = GEN_INT (2 * !dword);
      return "xxpermdi %x0,%x2,%x1,%4";
    }
}
  [(set_attr "type" "vecperm")])

(define_insn "*vsx_concat_<mode>_3"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_concat:VSX_D
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_D 1 "gpc_reg_operand" "wa")
	  (parallel [(match_operand:QI 2 "const_0_to_1_operand" "n")]))
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_D 3 "gpc_reg_operand" "wa")
	  (parallel [(match_operand:QI 4 "const_0_to_1_operand" "n")]))))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  HOST_WIDE_INT dword1 = INTVAL (operands[2]);
  HOST_WIDE_INT dword2 = INTVAL (operands[4]);
  if (BYTES_BIG_ENDIAN)
    {
      operands[5] = GEN_INT ((2 * dword1) + dword2);
      return "xxpermdi %x0,%x1,%x3,%5";
    }
  else
    {
      operands[5] = GEN_INT ((2 * !dword2) + !dword1);
      return "xxpermdi %x0,%x3,%x1,%5";
    }
}
  [(set_attr "type" "vecperm")])

;; Special purpose concat using xxpermdi to glue two single precision values
;; together, relying on the fact that internally scalar floats are represented
;; as doubles.  This is used to initialize a V4SF vector with 4 floats
(define_insn "vsx_concat_v2sf"
  [(set (match_operand:V2DF 0 "vsx_register_operand" "=wa")
	(unspec:V2DF
	 [(match_operand:SF 1 "vsx_register_operand" "wa")
	  (match_operand:SF 2 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_CONCAT))]
  "VECTOR_MEM_VSX_P (V2DFmode)"
{
  if (BYTES_BIG_ENDIAN)
    return "xxpermdi %x0,%x1,%x2,0";
  else
    return "xxpermdi %x0,%x2,%x1,0";
}
  [(set_attr "type" "vecperm")])

;; Concatenate 4 SImode elements into a V4SImode reg.
(define_expand "vsx_init_v4si"
  [(use (match_operand:V4SI 0 "gpc_reg_operand"))
   (use (match_operand:SI 1 "gpc_reg_operand"))
   (use (match_operand:SI 2 "gpc_reg_operand"))
   (use (match_operand:SI 3 "gpc_reg_operand"))
   (use (match_operand:SI 4 "gpc_reg_operand"))]
   "VECTOR_MEM_VSX_P (V4SImode) && TARGET_DIRECT_MOVE_64BIT"
{
  rtx a = gen_lowpart_SUBREG (DImode, operands[1]);
  rtx b = gen_lowpart_SUBREG (DImode, operands[2]);
  rtx c = gen_lowpart_SUBREG (DImode, operands[3]);
  rtx d = gen_lowpart_SUBREG (DImode, operands[4]);
  if (!BYTES_BIG_ENDIAN)
    {
      std::swap (a, b);
      std::swap (c, d);
    }

  rtx ab = gen_reg_rtx (DImode);
  rtx cd = gen_reg_rtx (DImode);
  emit_insn (gen_rotldi3_insert_3 (ab, a, GEN_INT (32), b,
				   GEN_INT (0xffffffff)));
  emit_insn (gen_rotldi3_insert_3 (cd, c, GEN_INT (32), d,
				   GEN_INT (0xffffffff)));

  rtx abcd = gen_reg_rtx (V2DImode);
  emit_insn (gen_vsx_concat_v2di (abcd, ab, cd));
  emit_move_insn (operands[0], gen_lowpart (V4SImode, abcd));
  DONE;
})

;; xxpermdi for little endian loads and stores.  We need several of
;; these since the form of the PARALLEL differs by mode.
(define_insn "*vsx_xxpermdi2_le_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
        (vec_select:VSX_D
          (match_operand:VSX_D 1 "vsx_register_operand" "wa")
          (parallel [(const_int 1) (const_int 0)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (<MODE>mode)"
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

(define_insn "xxswapd_v16qi"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(vec_select:V16QI
	  (match_operand:V16QI 1 "vsx_register_operand" "wa")
	  (parallel [(const_int 8) (const_int 9)
		     (const_int 10) (const_int 11)
		     (const_int 12) (const_int 13)
		     (const_int 14) (const_int 15)
		     (const_int 0) (const_int 1)
		     (const_int 2) (const_int 3)
		     (const_int 4) (const_int 5)
		     (const_int 6) (const_int 7)])))]
  "TARGET_VSX"
;; AIX does not support the extended mnemonic xxswapd.  Use the basic
;; mnemonic xxpermdi instead.
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

(define_insn "xxswapd_v8hi"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
	(vec_select:V8HI
	  (match_operand:V8HI 1 "vsx_register_operand" "wa")
	  (parallel [(const_int 4) (const_int 5)
		     (const_int 6) (const_int 7)
		     (const_int 0) (const_int 1)
		     (const_int 2) (const_int 3)])))]
  "TARGET_VSX"
;; AIX does not support the extended mnemonic xxswapd.  Use the basic
;; mnemonic xxpermdi instead.
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

(define_insn "xxswapd_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
	(vec_select:VSX_W
	  (match_operand:VSX_W 1 "vsx_register_operand" "wa")
	  (parallel [(const_int 2) (const_int 3)
		     (const_int 0) (const_int 1)])))]
  "TARGET_VSX"
;; AIX does not support extended mnemonic xxswapd.  Use the basic
;; mnemonic xxpermdi instead.
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

(define_insn "xxswapd_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_select:VSX_D
	  (match_operand:VSX_D 1 "vsx_register_operand" "wa")
	  (parallel [(const_int 1) (const_int 0)])))]
  "TARGET_VSX"
;; AIX does not support extended mnemonic xxswapd.  Use the basic
;; mnemonic xxpermdi instead.
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

;; Swap upper/lower 64-bit values in a 128-bit vector
(define_insn "xxswapd_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
     (subreg:V1TI
	  (vec_select:V2DI
	    (subreg:V2DI
	      (match_operand:V1TI 1 "vsx_register_operand" "v") 0 )
	  (parallel [(const_int 1)(const_int 0)]))
           0))]
  "TARGET_POWER10"
;; AIX does not support extended mnemonic xxswapd.  Use the basic
;; mnemonic xxpermdi instead.
  "xxpermdi %x0,%x1,%x1,2"
  [(set_attr "type" "vecperm")])

(define_insn "xxgenpcvm_<mode>_internal"
  [(set (match_operand:VSX_EXTRACT_I4 0 "altivec_register_operand" "=wa")
	(unspec:VSX_EXTRACT_I4
	 [(match_operand:VSX_EXTRACT_I4 1 "altivec_register_operand" "v")
	  (match_operand:QI 2 "const_0_to_3_operand" "n")]
	 UNSPEC_XXGENPCV))]
    "TARGET_POWER10"
    "xxgenpcv<wd>m %x0,%1,%2"
    [(set_attr "type" "vecsimple")])

(define_expand "xxgenpcvm_<mode>"
  [(use (match_operand:VSX_EXTRACT_I4 0 "register_operand"))
   (use (match_operand:VSX_EXTRACT_I4 1 "register_operand"))
   (use (match_operand:QI 2 "immediate_operand"))]
  "TARGET_POWER10"
{
  if (!BYTES_BIG_ENDIAN)
    {
      /* gen_xxgenpcvm assumes Big Endian order.  If LE,
	 change swap upper and lower double words.  */
      rtx tmp = gen_reg_rtx (<MODE>mode);

      emit_insn (gen_xxswapd_<mode> (tmp, operands[1]));
      operands[1] = tmp;
    }
    emit_insn (gen_xxgenpcvm_<mode>_internal (operands[0], operands[1],
					      operands[2]));
  DONE;
})

;; lxvd2x for little endian loads.  We need several of
;; these since the form of the PARALLEL differs by mode.
(define_insn "*vsx_lxvd2x2_le_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
        (vec_select:VSX_D
          (match_operand:VSX_D 1 "memory_operand" "Z")
          (parallel [(const_int 1) (const_int 0)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (<MODE>mode) && !TARGET_P9_VECTOR"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "*vsx_lxvd2x4_le_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
        (vec_select:VSX_W
          (match_operand:VSX_W 1 "memory_operand" "Z")
          (parallel [(const_int 2) (const_int 3)
                     (const_int 0) (const_int 1)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (<MODE>mode) && !TARGET_P9_VECTOR"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "*vsx_lxvd2x8_le_V8HI"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
        (vec_select:V8HI
          (match_operand:V8HI 1 "memory_operand" "Z")
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (V8HImode) && !TARGET_P9_VECTOR"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

(define_insn "*vsx_lxvd2x16_le_V16QI"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
        (vec_select:V16QI
          (match_operand:V16QI 1 "memory_operand" "Z")
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (V16QImode) && !TARGET_P9_VECTOR"
  "lxvd2x %x0,%y1"
  [(set_attr "type" "vecload")])

;; stxvd2x for little endian stores.  We need several of
;; these since the form of the PARALLEL differs by mode.
(define_insn "*vsx_stxvd2x2_le_<mode>"
  [(set (match_operand:VSX_D 0 "memory_operand" "=Z")
        (vec_select:VSX_D
          (match_operand:VSX_D 1 "vsx_register_operand" "wa")
          (parallel [(const_int 1) (const_int 0)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (<MODE>mode) && !TARGET_P9_VECTOR"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "*vsx_stxvd2x4_le_<mode>"
  [(set (match_operand:VSX_W 0 "memory_operand" "=Z")
        (vec_select:VSX_W
          (match_operand:VSX_W 1 "vsx_register_operand" "wa")
          (parallel [(const_int 2) (const_int 3)
                     (const_int 0) (const_int 1)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (<MODE>mode) && !TARGET_P9_VECTOR"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "*vsx_stxvd2x8_le_V8HI"
  [(set (match_operand:V8HI 0 "memory_operand" "=Z")
        (vec_select:V8HI
          (match_operand:V8HI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (V8HImode) && !TARGET_P9_VECTOR"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

(define_insn "*vsx_stxvd2x16_le_V16QI"
  [(set (match_operand:V16QI 0 "memory_operand" "=Z")
        (vec_select:V16QI
          (match_operand:V16QI 1 "vsx_register_operand" "wa")
          (parallel [(const_int 8) (const_int 9)
                     (const_int 10) (const_int 11)
                     (const_int 12) (const_int 13)
                     (const_int 14) (const_int 15)
                     (const_int 0) (const_int 1)
                     (const_int 2) (const_int 3)
                     (const_int 4) (const_int 5)
                     (const_int 6) (const_int 7)])))]
  "!BYTES_BIG_ENDIAN && VECTOR_MEM_VSX_P (V16QImode) && !TARGET_P9_VECTOR"
  "stxvd2x %x1,%y0"
  [(set_attr "type" "vecstore")])

;; Convert a TImode value into V1TImode
(define_expand "vsx_set_v1ti"
  [(match_operand:V1TI 0 "nonimmediate_operand")
   (match_operand:V1TI 1 "nonimmediate_operand")
   (match_operand:TI 2 "input_operand")
   (match_operand:QI 3 "u5bit_cint_operand")]
  "VECTOR_MEM_VSX_P (V1TImode)"
{
  if (operands[3] != const0_rtx)
    gcc_unreachable ();

  emit_move_insn (operands[0], gen_lowpart (V1TImode, operands[1]));
  DONE;
})

;; Rewrite V2DF/V2DI set in terms of VEC_CONCAT
(define_expand "vsx_set_<mode>"
  [(use (match_operand:VSX_D 0 "vsx_register_operand"))
   (use (match_operand:VSX_D 1 "vsx_register_operand"))
   (use (match_operand:<VS_scalar> 2 "gpc_reg_operand"))
   (use (match_operand:QI 3 "const_0_to_1_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtx dest = operands[0];
  rtx vec_reg = operands[1];
  rtx value = operands[2];
  rtx ele = operands[3];
  rtx tmp = gen_reg_rtx (<VS_scalar>mode);

  if (ele == const0_rtx)
    {
      emit_insn (gen_vsx_extract_<mode> (tmp, vec_reg, const1_rtx));
      emit_insn (gen_vsx_concat_<mode> (dest, value, tmp));
      DONE;
    }
  else if (ele == const1_rtx)
    {
      emit_insn (gen_vsx_extract_<mode> (tmp, vec_reg, const0_rtx));
      emit_insn (gen_vsx_concat_<mode> (dest, tmp, value));
      DONE;
    }
  else
    gcc_unreachable ();
})

;; Extract a DF/DI element from V2DF/V2DI
;; Optimize cases were we can do a simple or direct move.
;; Or see if we can avoid doing the move at all

(define_insn "vsx_extract_<mode>"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=wa, wa, wr, wr")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_D 1 "gpc_reg_operand"       "wa, wa, wa, wa")
	 (parallel
	  [(match_operand:QI 2 "const_0_to_1_operand"   "wD, n,  wD, n")])))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  int element = INTVAL (operands[2]);
  int op0_regno = REGNO (operands[0]);
  int op1_regno = REGNO (operands[1]);
  int fldDM;

  gcc_assert (IN_RANGE (element, 0, 1));
  gcc_assert (VSX_REGNO_P (op1_regno));

  if (element == VECTOR_ELEMENT_SCALAR_64BIT)
    {
      if (op0_regno == op1_regno)
	return ASM_COMMENT_START " vec_extract to same register";

      else if (INT_REGNO_P (op0_regno) && TARGET_DIRECT_MOVE
	       && TARGET_POWERPC64)
	return "mfvsrd %0,%x1";

      else if (FP_REGNO_P (op0_regno) && FP_REGNO_P (op1_regno))
	return "fmr %0,%1";

      else if (VSX_REGNO_P (op0_regno))
	return "xxlor %x0,%x1,%x1";

      else
	gcc_unreachable ();
    }

  else if (element == VECTOR_ELEMENT_MFVSRLD_64BIT && INT_REGNO_P (op0_regno)
	   && TARGET_P9_VECTOR && TARGET_POWERPC64 && TARGET_DIRECT_MOVE)
    return "mfvsrld %0,%x1";

  else if (VSX_REGNO_P (op0_regno))
    {
      fldDM = element << 1;
      if (!BYTES_BIG_ENDIAN)
	fldDM = 3 - fldDM;
      operands[3] = GEN_INT (fldDM);
      return "xxpermdi %x0,%x1,%x1,%3";
    }

  else
    gcc_unreachable ();
}
  [(set_attr "type" "veclogical,mfvsr,mfvsr,vecperm")
   (set_attr "isa" "*,*,p8v,p9v")])

;; Optimize extracting a single scalar element from memory.
(define_insn_and_split "*vsx_extract_<P:mode>_<VSX_D:mode>_load"
  [(set (match_operand:<VS_scalar> 0 "register_operand" "=wa,wr")
	(vec_select:<VSX_D:VS_scalar>
	 (match_operand:VSX_D 1 "memory_operand" "m,m")
	 (parallel [(match_operand:QI 2 "const_0_to_1_operand" "n,n")])))
   (clobber (match_scratch:P 3 "=&b,&b"))]
  "TARGET_POWERPC64 && VECTOR_MEM_VSX_P (<VSX_D:MODE>mode)"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], <VSX_D:VS_scalar>mode);
}
  [(set_attr "type" "fpload,load")
   (set_attr "length" "8")])

;; Optimize storing a single scalar element that is the right location to
;; memory
(define_insn "*vsx_extract_<mode>_store"
  [(set (match_operand:<VS_scalar> 0 "memory_operand" "=m,Z,wY")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_D 1 "register_operand" "d,v,v")
	 (parallel [(match_operand:QI 2 "vsx_scalar_64bit" "wD,wD,wD")])))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
  "@
   stfd%U0%X0 %1,%0
   stxsdx %x1,%y0
   stxsd %1,%0"
  [(set_attr "type" "fpstore")
   (set_attr "isa" "*,p7v,p9v")])

;; Variable V2DI/V2DF extract shift
(define_insn "vsx_vslo_<mode>"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=v")
	(unspec:<VS_scalar> [(match_operand:VSX_D 1 "gpc_reg_operand" "v")
			     (match_operand:V2DI 2 "gpc_reg_operand" "v")]
			    UNSPEC_VSX_VSLO))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "vslo %0,%1,%2"
  [(set_attr "type" "vecperm")])

;; Variable V2DI/V2DF extract from a register
(define_insn_and_split "vsx_extract_<mode>_var"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=v")
	(unspec:<VS_scalar> [(match_operand:VSX_D 1 "gpc_reg_operand" "v")
			     (match_operand:DI 2 "gpc_reg_operand" "r")]
			    UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=r"))
   (clobber (match_scratch:V2DI 4 "=&v"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_vec_extract_var (operands[0], operands[1], operands[2],
				operands[3], operands[4]);
  DONE;
})

;; Variable V2DI/V2DF extract from memory
(define_insn_and_split "*vsx_extract_<mode>_var_load"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=wa,r")
	(unspec:<VS_scalar> [(match_operand:VSX_D 1 "memory_operand" "Q,Q")
			     (match_operand:DI 2 "gpc_reg_operand" "r,r")]
			    UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=&b,&b"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], <VS_scalar>mode);
}
  [(set_attr "type" "fpload,load")])

;; Extract a SF element from V4SF
(define_insn_and_split "vsx_extract_v4sf"
  [(set (match_operand:SF 0 "vsx_register_operand" "=wa")
	(vec_select:SF
	 (match_operand:V4SF 1 "vsx_register_operand" "wa")
	 (parallel [(match_operand:QI 2 "u5bit_cint_operand" "n")])))
   (clobber (match_scratch:V4SF 3 "=0"))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op3 = operands[3];
  rtx tmp;
  HOST_WIDE_INT ele = BYTES_BIG_ENDIAN ? INTVAL (op2) : 3 - INTVAL (op2);

  if (ele == 0)
    tmp = op1;
  else
    {
      if (GET_CODE (op3) == SCRATCH)
	op3 = gen_reg_rtx (V4SFmode);
      emit_insn (gen_vsx_xxsldwi_v4sf (op3, op1, op1, GEN_INT (ele)));
      tmp = op3;
    }
  emit_insn (gen_vsx_xscvspdp_scalar2 (op0, tmp));
  DONE;
}
  [(set_attr "length" "8")
   (set_attr "type" "fp")])

(define_insn_and_split "*vsx_extract_v4sf_<mode>_load"
  [(set (match_operand:SF 0 "register_operand" "=f,v,v,?r")
	(vec_select:SF
	 (match_operand:V4SF 1 "memory_operand" "m,Z,m,m")
	 (parallel [(match_operand:QI 2 "const_0_to_3_operand" "n,n,n,n")])))
   (clobber (match_scratch:P 3 "=&b,&b,&b,&b"))]
  "VECTOR_MEM_VSX_P (V4SFmode)"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], SFmode);
}
  [(set_attr "type" "fpload,fpload,fpload,load")
   (set_attr "length" "8")
   (set_attr "isa" "*,p7v,p9v,*")])

;; Variable V4SF extract from a register
(define_insn_and_split "vsx_extract_v4sf_var"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=wa")
	(unspec:SF [(match_operand:V4SF 1 "gpc_reg_operand" "v")
		    (match_operand:DI 2 "gpc_reg_operand" "r")]
		   UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=r"))
   (clobber (match_scratch:V2DI 4 "=&v"))]
  "VECTOR_MEM_VSX_P (V4SFmode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_vec_extract_var (operands[0], operands[1], operands[2],
				operands[3], operands[4]);
  DONE;
})

;; Variable V4SF extract from memory
(define_insn_and_split "*vsx_extract_v4sf_var_load"
  [(set (match_operand:SF 0 "gpc_reg_operand" "=wa,?r")
	(unspec:SF [(match_operand:V4SF 1 "memory_operand" "Q,Q")
		    (match_operand:DI 2 "gpc_reg_operand" "r,r")]
		   UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=&b,&b"))]
  "VECTOR_MEM_VSX_P (V4SFmode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], SFmode);
}
  [(set_attr "type" "fpload,load")])

;; Expand the builtin form of xxpermdi to canonical rtl.
(define_expand "vsx_xxpermdi_<mode>"
  [(match_operand:VSX_L 0 "vsx_register_operand")
   (match_operand:VSX_L 1 "vsx_register_operand")
   (match_operand:VSX_L 2 "vsx_register_operand")
   (match_operand:QI 3 "u5bit_cint_operand")]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtx target = operands[0];
  rtx op0 = operands[1];
  rtx op1 = operands[2];
  int mask = INTVAL (operands[3]);
  rtx perm0 = GEN_INT ((mask >> 1) & 1);
  rtx perm1 = GEN_INT ((mask & 1) + 2);
  rtx (*gen) (rtx, rtx, rtx, rtx, rtx);

  if (<MODE>mode == V2DFmode)
    gen = gen_vsx_xxpermdi2_v2df_1;
  else
    {
      gen = gen_vsx_xxpermdi2_v2di_1;
      if (<MODE>mode != V2DImode)
	{
	  target = gen_lowpart (V2DImode, target);
	  op0 = gen_lowpart (V2DImode, op0);
	  op1 = gen_lowpart (V2DImode, op1);
	}
    }
  emit_insn (gen (target, op0, op1, perm0, perm1));
  DONE;
})

;; Special version of xxpermdi that retains big-endian semantics.
(define_expand "vsx_xxpermdi_<mode>_be"
  [(match_operand:VSX_L 0 "vsx_register_operand")
   (match_operand:VSX_L 1 "vsx_register_operand")
   (match_operand:VSX_L 2 "vsx_register_operand")
   (match_operand:QI 3 "u5bit_cint_operand")]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtx target = operands[0];
  rtx op0 = operands[1];
  rtx op1 = operands[2];
  int mask = INTVAL (operands[3]);
  rtx perm0 = GEN_INT ((mask >> 1) & 1);
  rtx perm1 = GEN_INT ((mask & 1) + 2);
  rtx (*gen) (rtx, rtx, rtx, rtx, rtx);

  if (<MODE>mode == V2DFmode)
    gen = gen_vsx_xxpermdi2_v2df_1;
  else
    {
      gen = gen_vsx_xxpermdi2_v2di_1;
      if (<MODE>mode != V2DImode)
	{
	  target = gen_lowpart (V2DImode, target);
	  op0 = gen_lowpart (V2DImode, op0);
	  op1 = gen_lowpart (V2DImode, op1);
	}
    }
  /* In little endian mode, vsx_xxpermdi2_<mode>_1 will perform a
     transformation we don't want; it is necessary for
     rs6000_expand_vec_perm_const_1 but not for this use.  So we
     prepare for that by reversing the transformation here.  */
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen (target, op0, op1, perm0, perm1));
  else
    {
      rtx p0 = GEN_INT (3 - INTVAL (perm1));
      rtx p1 = GEN_INT (3 - INTVAL (perm0));
      emit_insn (gen (target, op1, op0, p0, p1));
    }
  DONE;
})

(define_insn "vsx_xxpermdi2_<mode>_1"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_select:VSX_D
	  (vec_concat:<VS_double>
	    (match_operand:VSX_D 1 "vsx_register_operand" "wa")
	    (match_operand:VSX_D 2 "vsx_register_operand" "wa"))
	  (parallel [(match_operand 3 "const_0_to_1_operand" "")
		     (match_operand 4 "const_2_to_3_operand" "")])))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  int op3, op4, mask;

  /* For little endian, swap operands and invert/swap selectors
     to get the correct xxpermdi.  The operand swap sets up the
     inputs as a little endian array.  The selectors are swapped
     because they are defined to use big endian ordering.  The
     selectors are inverted to get the correct doublewords for
     little endian ordering.  */
  if (BYTES_BIG_ENDIAN)
    {
      op3 = INTVAL (operands[3]);
      op4 = INTVAL (operands[4]);
    }
  else
    {
      op3 = 3 - INTVAL (operands[4]);
      op4 = 3 - INTVAL (operands[3]);
    }

  mask = (op3 << 1) | (op4 - 2);
  operands[3] = GEN_INT (mask);

  if (BYTES_BIG_ENDIAN)
    return "xxpermdi %x0,%x1,%x2,%3";
  else
    return "xxpermdi %x0,%x2,%x1,%3";
}
  [(set_attr "type" "vecperm")])

;; Extraction of a single element in a small integer vector.  Until ISA 3.0,
;; none of the small types were allowed in a vector register, so we had to
;; extract to a DImode and either do a direct move or store.
(define_expand  "vsx_extract_<mode>"
  [(parallel [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand")
		   (vec_select:<VS_scalar>
		    (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand")
		    (parallel [(match_operand:QI 2 "const_int_operand")])))
	      (clobber (match_scratch:VSX_EXTRACT_I 3))])]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
{
  /* If we have ISA 3.0, we can do a xxextractuw/vextractu{b,h}.  */
  if (TARGET_P9_VECTOR)
    {
      emit_insn (gen_vsx_extract_<mode>_p9 (operands[0], operands[1],
					    operands[2]));
      DONE;
    }
})

(define_insn "vsx_extract_<mode>_p9"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=r,<VSX_EX>")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v,<VSX_EX>")
	 (parallel [(match_operand:QI 2 "<VSX_EXTRACT_PREDICATE>" "n,n")])))
   (clobber (match_scratch:SI 3 "=r,X"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_VEXTRACTUB"
{
  if (which_alternative == 0)
    return "#";

  else
    {
      HOST_WIDE_INT elt = INTVAL (operands[2]);
      HOST_WIDE_INT elt_adj = (!BYTES_BIG_ENDIAN
			       ? GET_MODE_NUNITS (<MODE>mode) - 1 - elt
			       : elt);

      HOST_WIDE_INT unit_size = GET_MODE_UNIT_SIZE (<MODE>mode);
      HOST_WIDE_INT offset = unit_size * elt_adj;

      operands[2] = GEN_INT (offset);
      if (unit_size == 4)
	return "xxextractuw %x0,%x1,%2";
      else
	return "vextractu<wd> %0,%1,%2";
    }
}
  [(set_attr "type" "vecsimple")
   (set_attr "isa" "p9v,*")])

(define_split
  [(set (match_operand:<VS_scalar> 0 "int_reg_operand")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_EXTRACT_I 1 "altivec_register_operand")
	 (parallel [(match_operand:QI 2 "const_int_operand")])))
   (clobber (match_operand:SI 3 "int_reg_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_VEXTRACTUB && reload_completed"
  [(const_int 0)]
{
  rtx op0_si = gen_rtx_REG (SImode, REGNO (operands[0]));
  rtx op1 = operands[1];
  rtx op2 = operands[2];
  rtx op3 = operands[3];
  HOST_WIDE_INT offset = INTVAL (op2) * GET_MODE_UNIT_SIZE (<MODE>mode);

  emit_move_insn (op3, GEN_INT (offset));
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vextu<wd>lx (op0_si, op3, op1));
  else
    emit_insn (gen_vextu<wd>rx (op0_si, op3, op1));
  DONE;
})

;; Optimize zero extracts to eliminate the AND after the extract.
(define_insn_and_split "*vsx_extract_<mode>_di_p9"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r,<VSX_EX>")
	(zero_extend:DI
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v,<VSX_EX>")
	  (parallel [(match_operand:QI 2 "const_int_operand" "n,n")]))))
   (clobber (match_scratch:SI 3 "=r,X"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_VEXTRACTUB"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 4)
		   (vec_select:<VS_scalar>
		    (match_dup 1)
		    (parallel [(match_dup 2)])))
	      (clobber (match_dup 3))])]
{
  operands[4] = gen_rtx_REG (<VS_scalar>mode, REGNO (operands[0]));
}
  [(set_attr "isa" "p9v,*")])

;; Optimize stores to use the ISA 3.0 scalar store instructions
(define_insn_and_split "*vsx_extract_<mode>_store_p9"
  [(set (match_operand:<VS_scalar> 0 "memory_operand" "=Z,m")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "<VSX_EX>,v")
	 (parallel [(match_operand:QI 2 "const_int_operand" "n,n")])))
   (clobber (match_scratch:<VS_scalar> 3 "=<VSX_EX>,&*r"))
   (clobber (match_scratch:SI 4 "=X,&r"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_VEXTRACTUB"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 3)
		   (vec_select:<VS_scalar>
		    (match_dup 1)
		    (parallel [(match_dup 2)])))
	      (clobber (match_dup 4))])
   (set (match_dup 0)
	(match_dup 3))])

(define_insn_and_split  "*vsx_extract_si"
  [(set (match_operand:SI 0 "nonimmediate_operand" "=r,wa,Z")
	(vec_select:SI
	 (match_operand:V4SI 1 "gpc_reg_operand" "v,v,v")
	 (parallel [(match_operand:QI 2 "const_0_to_3_operand" "n,n,n")])))
   (clobber (match_scratch:V4SI 3 "=v,v,v"))]
  "VECTOR_MEM_VSX_P (V4SImode) && TARGET_DIRECT_MOVE_64BIT && !TARGET_P9_VECTOR"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rtx dest = operands[0];
  rtx src = operands[1];
  rtx element = operands[2];
  rtx vec_tmp = operands[3];
  int value;

  /* Adjust index for LE element ordering, the below minuend 3 is computed by
     GET_MODE_NUNITS (V4SImode) - 1.  */
  if (!BYTES_BIG_ENDIAN)
    element = GEN_INT (3 - INTVAL (element));

  /* If the value is in the correct position, we can avoid doing the VSPLT<x>
     instruction.  */
  value = INTVAL (element);
  if (value != 1)
    emit_insn (gen_altivec_vspltw_direct (vec_tmp, src, element));
  else
    vec_tmp = src;

  if (MEM_P (operands[0]))
    {
      if (can_create_pseudo_p ())
	dest = rs6000_force_indexed_or_indirect_mem (dest);

      if (TARGET_P8_VECTOR)
	emit_move_insn (dest, gen_rtx_REG (SImode, REGNO (vec_tmp)));
      else
	emit_insn (gen_stfiwx (dest, gen_rtx_REG (DImode, REGNO (vec_tmp))));
    }

  else if (TARGET_P8_VECTOR)
    emit_move_insn (dest, gen_rtx_REG (SImode, REGNO (vec_tmp)));
  else
    emit_move_insn (gen_rtx_REG (DImode, REGNO (dest)),
		    gen_rtx_REG (DImode, REGNO (vec_tmp)));

  DONE;
}
  [(set_attr "type" "mfvsr,vecperm,fpstore")
   (set_attr "length" "8")
   (set_attr "isa" "*,p8v,*")])

(define_insn_and_split  "*vsx_extract_<mode>_p8"
  [(set (match_operand:<VS_scalar> 0 "nonimmediate_operand" "=r")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_EXTRACT_I2 1 "gpc_reg_operand" "v")
	 (parallel [(match_operand:QI 2 "<VSX_EXTRACT_PREDICATE>" "n")])))
   (clobber (match_scratch:VSX_EXTRACT_I2 3 "=v"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT
   && !TARGET_P9_VECTOR"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rtx dest = operands[0];
  rtx src = operands[1];
  rtx element = operands[2];
  rtx vec_tmp = operands[3];
  int value;

  if (!BYTES_BIG_ENDIAN)
    element = GEN_INT (GET_MODE_NUNITS (<MODE>mode) - 1 - INTVAL (element));

  /* If the value is in the correct position, we can avoid doing the VSPLT<x>
     instruction.  */
  value = INTVAL (element);
  if (<MODE>mode == V16QImode)
    {
      if (value != 7)
	emit_insn (gen_altivec_vspltb_direct (vec_tmp, src, element));
      else
	vec_tmp = src;
    }
  else if (<MODE>mode == V8HImode)
    {
      if (value != 3)
	emit_insn (gen_altivec_vsplth_direct (vec_tmp, src, element));
      else
	vec_tmp = src;
    }
  else
    gcc_unreachable ();

  emit_move_insn (gen_rtx_REG (DImode, REGNO (dest)),
		  gen_rtx_REG (DImode, REGNO (vec_tmp)));
  DONE;
}
  [(set_attr "type" "mfvsr")])

;; Optimize extracting a single scalar element from memory.
(define_insn_and_split "*vsx_extract_<mode>_load"
  [(set (match_operand:<VS_scalar> 0 "register_operand" "=r")
	(vec_select:<VS_scalar>
	 (match_operand:VSX_EXTRACT_I 1 "memory_operand" "m")
	 (parallel [(match_operand:QI 2 "<VSX_EXTRACT_PREDICATE>" "n")])))
   (clobber (match_scratch:DI 3 "=&b"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], <VS_scalar>mode);
}
  [(set_attr "type" "load")
   (set_attr "length" "8")])

;; Variable V16QI/V8HI/V4SI extract from a register
(define_insn_and_split "vsx_extract_<mode>_var"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=r,r")
	(unspec:<VS_scalar>
	 [(match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v,v")
	  (match_operand:DI 2 "gpc_reg_operand" "r,r")]
	 UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=r,r"))
   (clobber (match_scratch:V2DI 4 "=X,&v"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  rs6000_split_vec_extract_var (operands[0], operands[1], operands[2],
				operands[3], operands[4]);
  DONE;
}
  [(set_attr "isa" "p9v,*")])

;; Variable V16QI/V8HI/V4SI extract from memory
(define_insn_and_split "*vsx_extract_<mode>_var_load"
  [(set (match_operand:<VS_scalar> 0 "gpc_reg_operand" "=r")
	(unspec:<VS_scalar>
	 [(match_operand:VSX_EXTRACT_I 1 "memory_operand" "Q")
	  (match_operand:DI 2 "gpc_reg_operand" "r")]
	 UNSPEC_VSX_EXTRACT))
   (clobber (match_scratch:DI 3 "=&b"))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& reload_completed"
  [(set (match_dup 0) (match_dup 4))]
{
  operands[4] = rs6000_adjust_vec_address (operands[0], operands[1], operands[2],
					   operands[3], <VS_scalar>mode);
}
  [(set_attr "type" "load")])

;; ISA 3.1 extract
(define_expand "vextractl<mode>"
  [(set (match_operand:V2DI 0 "altivec_register_operand")
	(unspec:V2DI [(match_operand:VI2 1 "altivec_register_operand")
		      (match_operand:VI2 2 "altivec_register_operand")
		      (match_operand:SI 3 "register_operand")]
		     UNSPEC_EXTRACTL))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
    {
      emit_insn (gen_vextractl<mode>_internal (operands[0], operands[1],
					       operands[2], operands[3]));
      emit_insn (gen_xxswapd_v2di (operands[0], operands[0]));
    }
  else
    emit_insn (gen_vextractr<mode>_internal (operands[0], operands[2],
					     operands[1], operands[3]));
  DONE;
})

(define_insn "vextractl<mode>_internal"
  [(set (match_operand:V2DI 0 "altivec_register_operand" "=v")
	(unspec:V2DI [(match_operand:VEC_I 1 "altivec_register_operand" "v")
		      (match_operand:VEC_I 2 "altivec_register_operand" "v")
		      (match_operand:SI 3 "register_operand" "r")]
		     UNSPEC_EXTRACTL))]
  "TARGET_POWER10"
  "vext<du_or_d><wd>vlx %0,%1,%2,%3"
  [(set_attr "type" "vecsimple")])

(define_expand "vextractr<mode>"
  [(set (match_operand:V2DI 0 "altivec_register_operand")
	(unspec:V2DI [(match_operand:VI2 1 "altivec_register_operand")
		      (match_operand:VI2 2 "altivec_register_operand")
		      (match_operand:SI 3 "register_operand")]
		     UNSPEC_EXTRACTR))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
    {
      emit_insn (gen_vextractr<mode>_internal (operands[0], operands[1],
					       operands[2], operands[3]));
      emit_insn (gen_xxswapd_v2di (operands[0], operands[0]));
    }
  else
    emit_insn (gen_vextractl<mode>_internal (operands[0], operands[2],
					     operands[1], operands[3]));
  DONE;
})

(define_insn "vextractr<mode>_internal"
  [(set (match_operand:V2DI 0 "altivec_register_operand" "=v")
	(unspec:V2DI [(match_operand:VEC_I 1 "altivec_register_operand" "v")
		      (match_operand:VEC_I 2 "altivec_register_operand" "v")
		      (match_operand:SI 3 "register_operand" "r")]
		     UNSPEC_EXTRACTR))]
  "TARGET_POWER10"
  "vext<du_or_d><wd>vrx %0,%1,%2,%3"
  [(set_attr "type" "vecsimple")])

(define_expand "vinsertvl_<mode>"
  [(set (match_operand:VI2 0 "altivec_register_operand")
	(unspec:VI2 [(match_operand:VI2 1 "altivec_register_operand")
		     (match_operand:VI2 2 "altivec_register_operand")
		     (match_operand:SI 3 "register_operand" "r")]
		    UNSPEC_INSERTL))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
     emit_insn (gen_vinsertvl_internal_<mode> (operands[0], operands[3],
                                               operands[1], operands[2]));
   else
     emit_insn (gen_vinsertvr_internal_<mode> (operands[0], operands[3],
                                               operands[1], operands[2]));
   DONE;
})

(define_insn "vinsertvl_internal_<mode>"
  [(set (match_operand:VEC_I 0 "altivec_register_operand" "=v")
	(unspec:VEC_I [(match_operand:SI 1 "register_operand" "r")
		       (match_operand:VEC_I 2 "altivec_register_operand" "v")
		       (match_operand:VEC_I 3 "altivec_register_operand" "0")]
		      UNSPEC_INSERTL))]
  "TARGET_POWER10"
  "vins<wd>vlx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

(define_expand "vinsertvr_<mode>"
  [(set (match_operand:VI2 0 "altivec_register_operand")
	(unspec:VI2 [(match_operand:VI2 1 "altivec_register_operand")
		     (match_operand:VI2 2 "altivec_register_operand")
		     (match_operand:SI 3 "register_operand" "r")]
		    UNSPEC_INSERTR))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
     emit_insn (gen_vinsertvr_internal_<mode> (operands[0], operands[3],
                                               operands[1], operands[2]));
   else
     emit_insn (gen_vinsertvl_internal_<mode> (operands[0], operands[3],
                                               operands[1], operands[2]));
   DONE;
})

(define_insn "vinsertvr_internal_<mode>"
  [(set (match_operand:VEC_I 0 "altivec_register_operand" "=v")
	(unspec:VEC_I [(match_operand:SI 1 "register_operand" "r")
		       (match_operand:VEC_I 2 "altivec_register_operand" "v")
		       (match_operand:VEC_I 3 "altivec_register_operand" "0")]
		      UNSPEC_INSERTR))]
  "TARGET_POWER10"
  "vins<wd>vrx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

(define_expand "vinsertgl_<mode>"
  [(set (match_operand:VI2 0 "altivec_register_operand")
	(unspec:VI2 [(match_operand:SI 1 "register_operand")
		     (match_operand:VI2 2 "altivec_register_operand")
		     (match_operand:SI 3 "register_operand")]
		    UNSPEC_INSERTL))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vinsertgl_internal_<mode> (operands[0], operands[3],
                                            operands[1], operands[2]));
  else
    emit_insn (gen_vinsertgr_internal_<mode> (operands[0], operands[3],
                                            operands[1], operands[2]));
  DONE;
 })

(define_insn "vinsertgl_internal_<mode>"
 [(set (match_operand:VEC_I 0 "altivec_register_operand" "=v")
       (unspec:VEC_I [(match_operand:SI 1 "register_operand" "r")
		      (match_operand:SI 2 "register_operand" "r")
		      (match_operand:VEC_I 3 "altivec_register_operand" "0")]
		     UNSPEC_INSERTL))]
 "TARGET_POWER10"
 "vins<wd>lx %0,%1,%2"
 [(set_attr "type" "vecsimple")])

(define_expand "vinsertgr_<mode>"
  [(set (match_operand:VI2 0 "altivec_register_operand")
	(unspec:VI2 [(match_operand:SI 1 "register_operand")
		     (match_operand:VI2 2 "altivec_register_operand")
		     (match_operand:SI 3 "register_operand")]
		    UNSPEC_INSERTR))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_vinsertgr_internal_<mode> (operands[0], operands[3],
                                            operands[1], operands[2]));
  else
    emit_insn (gen_vinsertgl_internal_<mode> (operands[0], operands[3],
                                            operands[1], operands[2]));
  DONE;
 })

(define_insn "vinsertgr_internal_<mode>"
 [(set (match_operand:VEC_I 0 "altivec_register_operand" "=v")
   (unspec:VEC_I [(match_operand:SI 1 "register_operand" "r")
		  (match_operand:SI 2 "register_operand" "r")
		  (match_operand:VEC_I 3 "altivec_register_operand" "0")]
		 UNSPEC_INSERTR))]
 "TARGET_POWER10"
 "vins<wd>rx %0,%1,%2"
 [(set_attr "type" "vecsimple")])

(define_expand "vreplace_elt_<mode>"
  [(set (match_operand:REPLACE_ELT 0 "register_operand")
  (unspec:REPLACE_ELT [(match_operand:REPLACE_ELT 1 "register_operand")
		       (match_operand:<VS_scalar> 2 "register_operand")
		       (match_operand:QI 3 "const_0_to_3_operand")]
		      UNSPEC_REPLACE_ELT))]
 "TARGET_POWER10"
{
   int index;
   /* Immediate value is the word index, convert to byte index and adjust for
      Endianness if needed.  */
   if (BYTES_BIG_ENDIAN)
     index = INTVAL (operands[3]) << <REPLACE_ELT_sh>;

   else
     index = <REPLACE_ELT_max> - (INTVAL (operands[3]) << <REPLACE_ELT_sh>);

   emit_insn (gen_vreplace_elt_<mode>_inst (operands[0], operands[1],
					    operands[2],
					    GEN_INT (index)));
   DONE;
 }
[(set_attr "type" "vecsimple")])

(define_insn "vreplace_elt_<mode>_inst"
 [(set (match_operand:REPLACE_ELT 0 "register_operand" "=v")
  (unspec:REPLACE_ELT [(match_operand:REPLACE_ELT 1 "register_operand" "0")
		       (match_operand:<VS_scalar> 2 "register_operand" "r")
		       (match_operand:QI 3 "const_0_to_12_operand" "n")]
		      UNSPEC_REPLACE_ELT))]
 "TARGET_POWER10"
 "vins<REPLACE_ELT_char> %0,%2,%3"
 [(set_attr "type" "vecsimple")])

(define_insn "vreplace_un_<mode>"
 [(set (match_operand:V16QI 0 "register_operand" "=v")
  (unspec:V16QI [(match_operand:REPLACE_ELT 1 "register_operand" "0")
                 (match_operand:<VS_scalar> 2 "register_operand" "r")
		 (match_operand:QI 3 "const_0_to_12_operand" "n")]
		UNSPEC_REPLACE_UN))]
 "TARGET_POWER10"
 "vins<REPLACE_ELT_char> %0,%2,%3"
 [(set_attr "type" "vecsimple")])

;; VSX_EXTRACT optimizations
;; Optimize double d = (double) vec_extract (vi, <n>)
;; Get the element into the top position and use XVCVSWDP/XVCVUWDP
(define_insn_and_split "*vsx_extract_si_<uns>float_df"
  [(set (match_operand:DF 0 "gpc_reg_operand" "=wa")
	(any_float:DF
	 (vec_select:SI
	  (match_operand:V4SI 1 "gpc_reg_operand" "v")
	  (parallel [(match_operand:QI 2 "const_0_to_3_operand" "n")]))))
   (clobber (match_scratch:V4SI 3 "=v"))]
  "VECTOR_MEM_VSX_P (V4SImode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx dest = operands[0];
  rtx src = operands[1];
  rtx element = operands[2];
  rtx v4si_tmp = operands[3];
  int value;

  /* Adjust index for LE element ordering, the below minuend 3 is computed by
     GET_MODE_NUNITS (V4SImode) - 1.  */
  if (!BYTES_BIG_ENDIAN)
    element = GEN_INT (3 - INTVAL (element));

  /* If the value is in the correct position, we can avoid doing the VSPLT<x>
     instruction.  */
  value = INTVAL (element);
  if (value != 0)
    {
      if (GET_CODE (v4si_tmp) == SCRATCH)
	v4si_tmp = gen_reg_rtx (V4SImode);
      emit_insn (gen_altivec_vspltw_direct (v4si_tmp, src, element));
    }
  else
    v4si_tmp = src;

  emit_insn (gen_vsx_xvcv<su>xwdp_df (dest, v4si_tmp));
  DONE;
})

;; Optimize <type> f = (<type>) vec_extract (vi, <n>)
;; where <type> is a floating point type that supported by the hardware that is
;; not double.  First convert the value to double, and then to the desired
;; type.
(define_insn_and_split "*vsx_extract_si_<uns>float_<mode>"
  [(set (match_operand:VSX_EXTRACT_FL 0 "gpc_reg_operand" "=wa")
	(any_float:VSX_EXTRACT_FL
	 (vec_select:SI
	  (match_operand:V4SI 1 "gpc_reg_operand" "v")
	  (parallel [(match_operand:QI 2 "const_0_to_3_operand" "n")]))))
   (clobber (match_scratch:V4SI 3 "=v"))
   (clobber (match_scratch:DF 4 "=wa"))]
  "VECTOR_MEM_VSX_P (V4SImode) && TARGET_DIRECT_MOVE_64BIT"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx dest = operands[0];
  rtx src = operands[1];
  rtx element = operands[2];
  rtx v4si_tmp = operands[3];
  rtx df_tmp = operands[4];
  int value;

  /* Adjust index for LE element ordering, the below minuend 3 is computed by
     GET_MODE_NUNITS (V4SImode) - 1.  */
  if (!BYTES_BIG_ENDIAN)
    element = GEN_INT (3 - INTVAL (element));

  /* If the value is in the correct position, we can avoid doing the VSPLT<x>
     instruction.  */
  value = INTVAL (element);
  if (value != 0)
    {
      if (GET_CODE (v4si_tmp) == SCRATCH)
	v4si_tmp = gen_reg_rtx (V4SImode);
      emit_insn (gen_altivec_vspltw_direct (v4si_tmp, src, element));
    }
  else
    v4si_tmp = src;

  if (GET_CODE (df_tmp) == SCRATCH)
    df_tmp = gen_reg_rtx (DFmode);

  emit_insn (gen_vsx_xvcv<su>xwdp_df (df_tmp, v4si_tmp));

  if (<MODE>mode == SFmode)
    emit_insn (gen_truncdfsf2 (dest, df_tmp));
  else if (<MODE>mode == TFmode && FLOAT128_IBM_P (TFmode))
    emit_insn (gen_extenddftf2_vsx (dest, df_tmp));
  else if (<MODE>mode == TFmode && FLOAT128_IEEE_P (TFmode)
	   && TARGET_FLOAT128_HW)
    emit_insn (gen_extenddftf2_hw (dest, df_tmp));
  else if (<MODE>mode == IFmode && FLOAT128_IBM_P (IFmode))
    emit_insn (gen_extenddfif2 (dest, df_tmp));
  else if (<MODE>mode == KFmode && TARGET_FLOAT128_HW)
    emit_insn (gen_extenddfkf2_hw (dest, df_tmp));
  else
    gcc_unreachable ();

  DONE;
})

;; Optimize <type> f = (<ftype>) vec_extract (<vtype>, <n>)
;; Where <ftype> is SFmode, DFmode (and KFmode/TFmode if those types are IEEE
;; 128-bit hardware types) and <vtype> is vector char, vector unsigned char,
;; vector short or vector unsigned short.
(define_insn_and_split "*vsx_ext_<VSX_EXTRACT_I:VS_scalar>_fl_<FL_CONV:mode>"
  [(set (match_operand:FL_CONV 0 "gpc_reg_operand" "=wa")
	(float:FL_CONV
	 (vec_select:<VSX_EXTRACT_I:VS_scalar>
	  (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v")
	  (parallel [(match_operand:QI 2 "const_int_operand" "n")]))))
   (clobber (match_scratch:<VSX_EXTRACT_I:VS_scalar> 3 "=v"))]
  "VECTOR_MEM_VSX_P (<VSX_EXTRACT_I:MODE>mode) && TARGET_DIRECT_MOVE_64BIT
   && TARGET_P9_VECTOR"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 3)
		   (vec_select:<VSX_EXTRACT_I:VS_scalar>
		    (match_dup 1)
		    (parallel [(match_dup 2)])))
	      (clobber (scratch:SI))])
   (set (match_dup 4)
	(sign_extend:DI (match_dup 3)))
   (set (match_dup 0)
	(float:<FL_CONV:MODE> (match_dup 4)))]
{
  operands[4] = gen_rtx_REG (DImode, REGNO (operands[3]));
}
  [(set_attr "isa" "<FL_CONV:VSisa>")])

(define_insn_and_split "*vsx_ext_<VSX_EXTRACT_I:VS_scalar>_ufl_<FL_CONV:mode>"
  [(set (match_operand:FL_CONV 0 "gpc_reg_operand" "=wa")
	(unsigned_float:FL_CONV
	 (vec_select:<VSX_EXTRACT_I:VS_scalar>
	  (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v")
	  (parallel [(match_operand:QI 2 "const_int_operand" "n")]))))
   (clobber (match_scratch:<VSX_EXTRACT_I:VS_scalar> 3 "=v"))]
  "VECTOR_MEM_VSX_P (<VSX_EXTRACT_I:MODE>mode) && TARGET_DIRECT_MOVE_64BIT
   && TARGET_P9_VECTOR"
  "#"
  "&& reload_completed"
  [(parallel [(set (match_dup 3)
		   (vec_select:<VSX_EXTRACT_I:VS_scalar>
		    (match_dup 1)
		    (parallel [(match_dup 2)])))
	      (clobber (scratch:SI))])
   (set (match_dup 0)
	(float:<FL_CONV:MODE> (match_dup 4)))]
{
  operands[4] = gen_rtx_REG (DImode, REGNO (operands[3]));
}
  [(set_attr "isa" "<FL_CONV:VSisa>")])

;; V4SI/V8HI/V16QI set operation on ISA 3.0
(define_insn "vsx_set_<mode>_p9"
  [(set (match_operand:VSX_EXTRACT_I 0 "gpc_reg_operand" "=<VSX_EX>")
	(unspec:VSX_EXTRACT_I
	 [(match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "0")
	  (match_operand:<VS_scalar> 2 "gpc_reg_operand" "<VSX_EX>")
	  (match_operand:QI 3 "<VSX_EXTRACT_PREDICATE>" "n")]
	 UNSPEC_VSX_SET))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_P9_VECTOR && TARGET_POWERPC64"
{
  int ele = INTVAL (operands[3]);
  int nunits = GET_MODE_NUNITS (<MODE>mode);

  if (!BYTES_BIG_ENDIAN)
    ele = nunits - 1 - ele;

  operands[3] = GEN_INT (GET_MODE_SIZE (<VS_scalar>mode) * ele);
  if (<MODE>mode == V4SImode)
    return "xxinsertw %x0,%x2,%3";
  else
    return "vinsert<wd> %0,%2,%3";
}
  [(set_attr "type" "vecperm")])

(define_insn_and_split "vsx_set_v4sf_p9"
  [(set (match_operand:V4SF 0 "gpc_reg_operand" "=wa")
	(unspec:V4SF
	 [(match_operand:V4SF 1 "gpc_reg_operand" "0")
	  (match_operand:SF 2 "gpc_reg_operand" "wa")
	  (match_operand:QI 3 "const_0_to_3_operand" "n")]
	 UNSPEC_VSX_SET))
   (clobber (match_scratch:SI 4 "=&wa"))]
  "VECTOR_MEM_VSX_P (V4SFmode) && TARGET_P9_VECTOR && TARGET_POWERPC64"
  "#"
  "&& reload_completed"
  [(set (match_dup 5)
	(unspec:V4SF [(match_dup 2)]
		     UNSPEC_VSX_CVDPSPN))
   (parallel [(set (match_dup 4)
		   (vec_select:SI (match_dup 6)
				  (parallel [(match_dup 7)])))
	      (clobber (scratch:SI))])
   (set (match_dup 8)
	(unspec:V4SI [(match_dup 8)
		      (match_dup 4)
		      (match_dup 3)]
		     UNSPEC_VSX_SET))]
{
  unsigned int tmp_regno = reg_or_subregno (operands[4]);

  operands[5] = gen_rtx_REG (V4SFmode, tmp_regno);
  operands[6] = gen_rtx_REG (V4SImode, tmp_regno);
  operands[7] = GEN_INT (BYTES_BIG_ENDIAN ? 0 : 3);
  operands[8] = gen_rtx_REG (V4SImode, reg_or_subregno (operands[0]));
}
  [(set_attr "type" "vecperm")
   (set_attr "length" "12")
   (set_attr "isa" "p9v")])

;; Special case setting 0.0f to a V4SF element
(define_insn_and_split "*vsx_set_v4sf_p9_zero"
  [(set (match_operand:V4SF 0 "gpc_reg_operand" "=wa")
	(unspec:V4SF
	 [(match_operand:V4SF 1 "gpc_reg_operand" "0")
	  (match_operand:SF 2 "zero_fp_constant" "j")
	  (match_operand:QI 3 "const_0_to_3_operand" "n")]
	 UNSPEC_VSX_SET))
   (clobber (match_scratch:SI 4 "=&wa"))]
  "VECTOR_MEM_VSX_P (V4SFmode) && TARGET_P9_VECTOR && TARGET_POWERPC64"
  "#"
  "&& reload_completed"
  [(set (match_dup 4)
	(const_int 0))
   (set (match_dup 5)
	(unspec:V4SI [(match_dup 5)
		      (match_dup 4)
		      (match_dup 3)]
		     UNSPEC_VSX_SET))]
{
  operands[5] = gen_rtx_REG (V4SImode, reg_or_subregno (operands[0]));
}
  [(set_attr "type" "vecperm")
   (set_attr "length" "8")
   (set_attr "isa" "p9v")])

;; Optimize x = vec_insert (vec_extract (v2, n), v1, m) if n is the element
;; that is in the default scalar position (1 for big endian, 2 for little
;; endian).  We just need to do an xxinsertw since the element is in the
;; correct location.

(define_insn "*vsx_insert_extract_v4sf_p9"
  [(set (match_operand:V4SF 0 "gpc_reg_operand" "=wa")
	(unspec:V4SF
	 [(match_operand:V4SF 1 "gpc_reg_operand" "0")
	  (vec_select:SF (match_operand:V4SF 2 "gpc_reg_operand" "wa")
			 (parallel
			  [(match_operand:QI 3 "const_0_to_3_operand" "n")]))
	  (match_operand:QI 4 "const_0_to_3_operand" "n")]
	 UNSPEC_VSX_SET))]
  "VECTOR_MEM_VSX_P (V4SFmode) && TARGET_P9_VECTOR && TARGET_POWERPC64
   && (INTVAL (operands[3]) == (BYTES_BIG_ENDIAN ? 1 : 2))"
{
  int ele = INTVAL (operands[4]);

  /* Adjust index for LE element ordering, the below minuend 3 is computed by
     GET_MODE_NUNITS (V4SFmode) - 1.  */
  if (!BYTES_BIG_ENDIAN)
    ele = 3 - ele;

  operands[4] = GEN_INT (GET_MODE_SIZE (SFmode) * ele);
  return "xxinsertw %x0,%x2,%4";
}
  [(set_attr "type" "vecperm")])

;; Optimize x = vec_insert (vec_extract (v2, n), v1, m) if n is not the element
;; that is in the default scalar position (1 for big endian, 2 for little
;; endian).  Convert the insert/extract to int and avoid doing the conversion.

(define_insn_and_split "*vsx_insert_extract_v4sf_p9_2"
  [(set (match_operand:V4SF 0 "gpc_reg_operand" "=wa")
	(unspec:V4SF
	 [(match_operand:V4SF 1 "gpc_reg_operand" "0")
	  (vec_select:SF (match_operand:V4SF 2 "gpc_reg_operand" "wa")
			 (parallel
			  [(match_operand:QI 3 "const_0_to_3_operand" "n")]))
	  (match_operand:QI 4 "const_0_to_3_operand" "n")]
	 UNSPEC_VSX_SET))
   (clobber (match_scratch:SI 5 "=&wa"))]
  "VECTOR_MEM_VSX_P (V4SFmode) && VECTOR_MEM_VSX_P (V4SImode)
   && TARGET_P9_VECTOR && TARGET_POWERPC64
   && (INTVAL (operands[3]) != (BYTES_BIG_ENDIAN ? 1 : 2))"
  "#"
  "&& 1"
  [(parallel [(set (match_dup 5)
		   (vec_select:SI (match_dup 6)
				  (parallel [(match_dup 3)])))
	      (clobber (scratch:SI))])
   (set (match_dup 7)
	(unspec:V4SI [(match_dup 8)
		      (match_dup 5)
		      (match_dup 4)]
		     UNSPEC_VSX_SET))]
{
  if (GET_CODE (operands[5]) == SCRATCH)
    operands[5] = gen_reg_rtx (SImode);

  operands[6] = gen_lowpart (V4SImode, operands[2]);
  operands[7] = gen_lowpart (V4SImode, operands[0]);
  operands[8] = gen_lowpart (V4SImode, operands[1]);
}
  [(set_attr "type" "vecperm")
   (set_attr "isa" "p9v")])

;; Expanders for builtins
(define_expand "vsx_mergel_<mode>"
  [(use (match_operand:VSX_D 0 "vsx_register_operand"))
   (use (match_operand:VSX_D 1 "vsx_register_operand"))
   (use (match_operand:VSX_D 2 "vsx_register_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtvec v = gen_rtvec (2, GEN_INT (1), GEN_INT (3));
  rtx x = gen_rtx_VEC_CONCAT (<VS_double>mode, operands[1], operands[2]);
  x = gen_rtx_VEC_SELECT (<MODE>mode, x, gen_rtx_PARALLEL (VOIDmode, v));
  emit_insn (gen_rtx_SET (operands[0], x));
  DONE;
})

(define_expand "vsx_mergeh_<mode>"
  [(use (match_operand:VSX_D 0 "vsx_register_operand"))
   (use (match_operand:VSX_D 1 "vsx_register_operand"))
   (use (match_operand:VSX_D 2 "vsx_register_operand"))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtvec v = gen_rtvec (2, GEN_INT (0), GEN_INT (2));
  rtx x = gen_rtx_VEC_CONCAT (<VS_double>mode, operands[1], operands[2]);
  x = gen_rtx_VEC_SELECT (<MODE>mode, x, gen_rtx_PARALLEL (VOIDmode, v));
  emit_insn (gen_rtx_SET (operands[0], x));
  DONE;
})

;; V2DF/V2DI splat
;; We separate the register splat insn from the memory splat insn to force the
;; register allocator to generate the indexed form of the SPLAT when it is
;; given an offsettable memory reference.  Otherwise, if the register and
;; memory insns were combined into a single insn, the register allocator will
;; load the value into a register, and then do a double word permute.
(define_expand "vsx_splat_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand")
	(vec_duplicate:VSX_D
	 (match_operand:<VS_scalar> 1 "input_operand")))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  rtx op1 = operands[1];
  if (MEM_P (op1))
    operands[1] = rs6000_force_indexed_or_indirect_mem (op1);
  else
    operands[1] = force_reg (<VSX_D:VS_scalar>mode, op1);
})

(define_insn "vsx_splat_<mode>_reg"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa,we")
	(vec_duplicate:VSX_D
	 (match_operand:<VS_scalar> 1 "gpc_reg_operand" "wa,b")))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
  "@
   xxpermdi %x0,%x1,%x1,0
   mtvsrdd %x0,%1,%1"
  [(set_attr "type" "vecperm,vecmove")])

(define_insn "vsx_splat_<mode>_mem"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
	(vec_duplicate:VSX_D
	 (match_operand:<VSX_D:VS_scalar> 1 "memory_operand" "Z")))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
  "lxvdsx %x0,%y1"
  [(set_attr "type" "vecload")])

;; V4SI splat support
(define_insn "vsx_splat_v4si"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=wa,wa")
	(vec_duplicate:V4SI
	 (match_operand:SI 1 "splat_input_operand" "r,Z")))]
  "TARGET_P9_VECTOR"
  "@
   mtvsrws %x0,%1
   lxvwsx %x0,%y1"
  [(set_attr "type" "vecperm,vecload")])

;; SImode is not currently allowed in vector registers.  This pattern
;; allows us to use direct move to get the value in a vector register
;; so that we can use XXSPLTW
(define_insn "vsx_splat_v4si_di"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=wa,we")
	(vec_duplicate:V4SI
	 (truncate:SI
	  (match_operand:DI 1 "gpc_reg_operand" "wa,r"))))]
  "VECTOR_MEM_VSX_P (V4SImode) && TARGET_DIRECT_MOVE_64BIT"
  "@
   xxspltw %x0,%x1,1
   mtvsrws %x0,%1"
  [(set_attr "type" "vecperm")
   (set_attr "isa" "p8v,*")])

;; V4SF splat (ISA 3.0)
(define_insn_and_split "vsx_splat_v4sf"
  [(set (match_operand:V4SF 0 "vsx_register_operand" "=wa,wa,wa")
	(vec_duplicate:V4SF
	 (match_operand:SF 1 "splat_input_operand" "Z,wa,r")))]
  "TARGET_P9_VECTOR"
  "@
   lxvwsx %x0,%y1
   #
   mtvsrws %x0,%1"
  "&& reload_completed && vsx_register_operand (operands[1], SFmode)"
  [(set (match_dup 0)
	(unspec:V4SF [(match_dup 1)] UNSPEC_VSX_CVDPSPN))
   (set (match_dup 0)
	(unspec:V4SF [(match_dup 0)
		      (const_int 0)] UNSPEC_VSX_XXSPLTW))]
  ""
  [(set_attr "type" "vecload,vecperm,vecperm")
   (set_attr "length" "*,8,*")
   (set_attr "isa" "*,p8v,*")])

;; V4SF/V4SI splat from a vector element
(define_insn "vsx_xxspltw_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
	(vec_duplicate:VSX_W
	 (vec_select:<VS_scalar>
	  (match_operand:VSX_W 1 "vsx_register_operand" "wa")
	  (parallel
	   [(match_operand:QI 2 "u5bit_cint_operand" "n")]))))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  if (!BYTES_BIG_ENDIAN)
    operands[2] = GEN_INT (3 - INTVAL (operands[2]));

  return "xxspltw %x0,%x1,%2";
}
  [(set_attr "type" "vecperm")])

(define_insn "vsx_xxspltw_<mode>_direct"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
        (unspec:VSX_W [(match_operand:VSX_W 1 "vsx_register_operand" "wa")
                       (match_operand:QI 2 "u5bit_cint_operand" "i")]
                      UNSPEC_VSX_XXSPLTW))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
  "xxspltw %x0,%x1,%2"
  [(set_attr "type" "vecperm")])

;; V16QI/V8HI splat support on ISA 2.07
(define_insn "vsx_vsplt<VSX_SPLAT_SUFFIX>_di"
  [(set (match_operand:VSX_SPLAT_I 0 "altivec_register_operand" "=v")
	(vec_duplicate:VSX_SPLAT_I
	 (truncate:<VS_scalar>
	  (match_operand:DI 1 "altivec_register_operand" "v"))))]
  "VECTOR_MEM_VSX_P (<MODE>mode) && TARGET_DIRECT_MOVE_64BIT"
  "vsplt<VSX_SPLAT_SUFFIX> %0,%1,<VSX_SPLAT_COUNT>"
  [(set_attr "type" "vecperm")])

;; V2DF/V2DI splat for use by vec_splat builtin
(define_insn "vsx_xxspltd_<mode>"
  [(set (match_operand:VSX_D 0 "vsx_register_operand" "=wa")
        (unspec:VSX_D [(match_operand:VSX_D 1 "vsx_register_operand" "wa")
	               (match_operand:QI 2 "u5bit_cint_operand" "i")]
                      UNSPEC_VSX_XXSPLTD))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  if ((BYTES_BIG_ENDIAN && INTVAL (operands[2]) == 0)
      || (!BYTES_BIG_ENDIAN && INTVAL (operands[2]) == 1))
    return "xxpermdi %x0,%x1,%x1,0";
  else
    return "xxpermdi %x0,%x1,%x1,3";
}
  [(set_attr "type" "vecperm")])

;; V4SF/V4SI interleave
(define_expand "vsx_xxmrghw_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
        (vec_select:VSX_W
	  (vec_concat:<VS_double>
	    (match_operand:VSX_W 1 "vsx_register_operand" "wa")
	    (match_operand:VSX_W 2 "vsx_register_operand" "wa"))
	  (parallel [(const_int 0) (const_int 4)
		     (const_int 1) (const_int 5)])))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_altivec_vmrghw_direct_v4si_be (operands[0],
						  operands[1],
						  operands[2]));
  else
    emit_insn (gen_altivec_vmrglw_direct_v4si_le (operands[0],
						  operands[2],
						  operands[1]));
  DONE;
}
  [(set_attr "type" "vecperm")])

(define_expand "vsx_xxmrglw_<mode>"
  [(set (match_operand:VSX_W 0 "vsx_register_operand" "=wa")
	(vec_select:VSX_W
	  (vec_concat:<VS_double>
	    (match_operand:VSX_W 1 "vsx_register_operand" "wa")
	    (match_operand:VSX_W 2 "vsx_register_operand" "wa"))
	  (parallel [(const_int 2) (const_int 6)
		     (const_int 3) (const_int 7)])))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_altivec_vmrglw_direct_v4si_be (operands[0],
						  operands[1],
						  operands[2]));
  else
    emit_insn (gen_altivec_vmrghw_direct_v4si_le (operands[0],
						  operands[2],
						  operands[1]));
  DONE;
}
  [(set_attr "type" "vecperm")])

;; Shift left double by word immediate
(define_insn "vsx_xxsldwi_<mode>"
  [(set (match_operand:VSX_L 0 "vsx_register_operand" "=wa")
	(unspec:VSX_L [(match_operand:VSX_L 1 "vsx_register_operand" "wa")
		       (match_operand:VSX_L 2 "vsx_register_operand" "wa")
		       (match_operand:QI 3 "u5bit_cint_operand" "i")]
		      UNSPEC_VSX_SLDWI))]
  "VECTOR_MEM_VSX_P (<MODE>mode)"
  "xxsldwi %x0,%x1,%x2,%3"
  [(set_attr "type" "vecperm")
   (set_attr "isa" "<VSisa>")])


;; Vector reduction insns and splitters

(define_insn_and_split "vsx_reduc_<VEC_reduc_name>_v2df"
  [(set (match_operand:V2DF 0 "vfloat_operand" "=&wa,wa")
	(VEC_reduc:V2DF
	 (vec_concat:V2DF
	  (vec_select:DF
	   (match_operand:V2DF 1 "vfloat_operand" "wa,wa")
	   (parallel [(const_int 1)]))
	  (vec_select:DF
	   (match_dup 1)
	   (parallel [(const_int 0)])))
	 (match_dup 1)))
   (clobber (match_scratch:V2DF 2 "=0,&wa"))]
  "VECTOR_UNIT_VSX_P (V2DFmode)"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx tmp = (GET_CODE (operands[2]) == SCRATCH)
	     ? gen_reg_rtx (V2DFmode)
	     : operands[2];
  emit_insn (gen_vsx_xxsldwi_v2df (tmp, operands[1], operands[1], const2_rtx));
  emit_insn (gen_<VEC_reduc_rtx>v2df3 (operands[0], tmp, operands[1]));
  DONE;
}
  [(set_attr "length" "8")
   (set_attr "type" "veccomplex")])

(define_insn_and_split "vsx_reduc_<VEC_reduc_name>_v4sf"
  [(set (match_operand:V4SF 0 "vfloat_operand" "=wa")
	(VEC_reduc:V4SF
	 (unspec:V4SF [(const_int 0)] UNSPEC_REDUC)
	 (match_operand:V4SF 1 "vfloat_operand" "wa")))
   (clobber (match_scratch:V4SF 2 "=&wa"))
   (clobber (match_scratch:V4SF 3 "=&wa"))]
  "VECTOR_UNIT_VSX_P (V4SFmode)"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx tmp2, tmp3, tmp4;

  if (can_create_pseudo_p ())
    {
      tmp2 = gen_reg_rtx (V4SFmode);
      tmp3 = gen_reg_rtx (V4SFmode);
      tmp4 = gen_reg_rtx (V4SFmode);
    }
  else
    {
      tmp2 = operands[2];
      tmp3 = operands[3];
      tmp4 = tmp2;
    }

  emit_insn (gen_vsx_xxsldwi_v4sf (tmp2, op1, op1, const2_rtx));
  emit_insn (gen_<VEC_reduc_rtx>v4sf3 (tmp3, tmp2, op1));
  emit_insn (gen_vsx_xxsldwi_v4sf (tmp4, tmp3, tmp3, GEN_INT (3)));
  emit_insn (gen_<VEC_reduc_rtx>v4sf3 (op0, tmp4, tmp3));
  DONE;
}
  [(set_attr "length" "16")
   (set_attr "type" "veccomplex")])

;; Combiner patterns with the vector reduction patterns that knows we can get
;; to the top element of the V2DF array without doing an extract.

(define_insn_and_split "*vsx_reduc_<VEC_reduc_name>_v2df_scalar"
  [(set (match_operand:DF 0 "vfloat_operand" "=&wa,wa")
	(vec_select:DF
	 (VEC_reduc:V2DF
	  (vec_concat:V2DF
	   (vec_select:DF
	    (match_operand:V2DF 1 "vfloat_operand" "wa,wa")
	    (parallel [(const_int 1)]))
	   (vec_select:DF
	    (match_dup 1)
	    (parallel [(const_int 0)])))
	  (match_dup 1))
	 (parallel [(const_int 1)])))
   (clobber (match_scratch:DF 2 "=0,&wa"))]
  "BYTES_BIG_ENDIAN && VECTOR_UNIT_VSX_P (V2DFmode)"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx hi = gen_highpart (DFmode, operands[1]);
  rtx lo = (GET_CODE (operands[2]) == SCRATCH)
	    ? gen_reg_rtx (DFmode)
	    : operands[2];

  emit_insn (gen_vsx_extract_v2df (lo, operands[1], const1_rtx));
  emit_insn (gen_<VEC_reduc_rtx>df3 (operands[0], hi, lo));
  DONE;
}
  [(set_attr "length" "8")
   (set_attr "type" "veccomplex")])

(define_insn_and_split "*vsx_reduc_<VEC_reduc_name>_v4sf_scalar"
  [(set (match_operand:SF 0 "vfloat_operand" "=f")
	(vec_select:SF
	 (VEC_reduc:V4SF
	  (unspec:V4SF [(const_int 0)] UNSPEC_REDUC)
	  (match_operand:V4SF 1 "vfloat_operand" "wa"))
	 (parallel [(const_int 3)])))
   (clobber (match_scratch:V4SF 2 "=&wa"))
   (clobber (match_scratch:V4SF 3 "=&wa"))
   (clobber (match_scratch:V4SF 4 "=0"))]
  "BYTES_BIG_ENDIAN && VECTOR_UNIT_VSX_P (V4SFmode)"
  "#"
  "&& 1"
  [(const_int 0)]
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];
  rtx tmp2, tmp3, tmp4, tmp5;

  if (can_create_pseudo_p ())
    {
      tmp2 = gen_reg_rtx (V4SFmode);
      tmp3 = gen_reg_rtx (V4SFmode);
      tmp4 = gen_reg_rtx (V4SFmode);
      tmp5 = gen_reg_rtx (V4SFmode);
    }
  else
    {
      tmp2 = operands[2];
      tmp3 = operands[3];
      tmp4 = tmp2;
      tmp5 = operands[4];
    }

  emit_insn (gen_vsx_xxsldwi_v4sf (tmp2, op1, op1, const2_rtx));
  emit_insn (gen_<VEC_reduc_rtx>v4sf3 (tmp3, tmp2, op1));
  emit_insn (gen_vsx_xxsldwi_v4sf (tmp4, tmp3, tmp3, GEN_INT (3)));
  emit_insn (gen_<VEC_reduc_rtx>v4sf3 (tmp5, tmp4, tmp3));
  emit_insn (gen_vsx_xscvspdp_scalar2 (op0, tmp5));
  DONE;
}
  [(set_attr "length" "20")
   (set_attr "type" "veccomplex")])


;; Power8 Vector fusion.  The fused ops must be physically adjacent.
(define_peephole
  [(set (match_operand:P 0 "base_reg_operand")
	(match_operand:P 1 "short_cint_operand"))
   (set (match_operand:VSX_M 2 "vsx_register_operand")
	(mem:VSX_M (plus:P (match_dup 0)
			   (match_operand:P 3 "int_reg_operand"))))]
  "TARGET_VSX && TARGET_P8_FUSION && !TARGET_P9_VECTOR"
  "li %0,%1\;lx<VSX_M:VSm>x %x2,%0,%3\t\t\t# vector load fusion"
  [(set_attr "length" "8")
   (set_attr "type" "vecload")])

(define_peephole
  [(set (match_operand:P 0 "base_reg_operand")
	(match_operand:P 1 "short_cint_operand"))
   (set (match_operand:VSX_M 2 "vsx_register_operand")
	(mem:VSX_M (plus:P (match_operand:P 3 "int_reg_operand")
			   (match_dup 0))))]
  "TARGET_VSX && TARGET_P8_FUSION && !TARGET_P9_VECTOR"
  "li %0,%1\;lx<VSX_M:VSm>x %x2,%0,%3\t\t\t# vector load fusion"
  [(set_attr "length" "8")
   (set_attr "type" "vecload")])


;; ISA 3.1 vector extend sign support
(define_insn "vsx_sign_extend_v2di_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=v")
	(unspec:V1TI [(match_operand:V2DI 1 "vsx_register_operand" "v")]
		     UNSPEC_VSX_SIGN_EXTEND))]
  "TARGET_POWER10"
 "vextsd2q %0,%1"
[(set_attr "type" "vecexts")])

;; ISA 3.0 vector extend sign support

(define_insn "vsx_sign_extend_v16qi_<mode>"
  [(set (match_operand:VSINT_84 0 "vsx_register_operand" "=v")
	(unspec:VSINT_84
	 [(match_operand:V16QI 1 "vsx_register_operand" "v")]
	 UNSPEC_VSX_SIGN_EXTEND))]
  "TARGET_P9_VECTOR"
  "vextsb2<wd> %0,%1"
  [(set_attr "type" "vecexts")])

(define_insn "vsx_sign_extend_v8hi_<mode>"
  [(set (match_operand:VSINT_84 0 "vsx_register_operand" "=v")
	(unspec:VSINT_84
	 [(match_operand:V8HI 1 "vsx_register_operand" "v")]
	 UNSPEC_VSX_SIGN_EXTEND))]
  "TARGET_P9_VECTOR"
  "vextsh2<wd> %0,%1"
  [(set_attr "type" "vecexts")])

(define_insn "vsx_sign_extend_v4si_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=v")
	(unspec:V2DI [(match_operand:V4SI 1 "vsx_register_operand" "v")]
		     UNSPEC_VSX_SIGN_EXTEND))]
  "TARGET_P9_VECTOR"
  "vextsw2d %0,%1"
  [(set_attr "type" "vecexts")])

;; Sign extend DI to TI.  We provide both GPR targets and Altivec targets on
;; power10.  On earlier systems, the machine independent code will generate a
;; shift left to sign extend the 64-bit value to 128-bit.
;;
;; If the register allocator prefers to use GPR registers, we will use a shift
;; left instruction to sign extend the 64-bit value to 128-bit.
;;
;; If the register allocator prefers to use Altivec registers on power10,
;; generate the vextsd2q instruction.
(define_insn_and_split "extendditi2"
  [(set (match_operand:TI 0 "register_operand" "=r,r,v,v,v")
	(sign_extend:TI (match_operand:DI 1 "input_operand" "r,m,b,wa,Z")))
   (clobber (reg:DI CA_REGNO))]
  "TARGET_POWERPC64 && TARGET_POWER10"
  "#"
  "&& reload_completed"
  [(pc)]
{
  rtx dest = operands[0];
  rtx src = operands[1];
  int dest_regno = reg_or_subregno (dest);

  /* Handle conversion to GPR registers.  Load up the low part and then do
     a sign extension to the upper part.  */
  if (INT_REGNO_P (dest_regno))
    {
      rtx dest_hi = gen_highpart (DImode, dest);
      rtx dest_lo = gen_lowpart (DImode, dest);

      emit_move_insn (dest_lo, src);
      /* In case src is a MEM, we have to use the destination, which is a
         register, instead of re-using the source.  */
      rtx src2 = (REG_P (src) || SUBREG_P (src)) ? src : dest_lo;
      emit_insn (gen_ashrdi3 (dest_hi, src2, GEN_INT (63)));
      DONE;
    }

  /* For conversion to an Altivec register, generate either a splat operation
     or a load rightmost double word instruction.  Both instructions gets the
     DImode value into the lower 64 bits, and then do the vextsd2q
     instruction.  */

  else if (ALTIVEC_REGNO_P (dest_regno))
    {
      if (MEM_P (src))
	emit_insn (gen_vsx_lxvrdx (dest, src));
      else
	{
	  rtx dest_v2di = gen_rtx_REG (V2DImode, dest_regno);
	  emit_insn (gen_vsx_splat_v2di (dest_v2di, src));
	}

      emit_insn (gen_extendditi2_vector (dest, dest));
      DONE;
    }

  else
    gcc_unreachable ();
}
  [(set_attr "length" "8")
   (set_attr "type" "shift,load,vecmove,vecperm,load")])

;; Sign extend 64-bit value in TI reg, word 1, to 128-bit value in TI reg
(define_insn "extendditi2_vector"
  [(set (match_operand:TI 0 "gpc_reg_operand" "=v")
	(unspec:TI [(match_operand:TI 1 "gpc_reg_operand" "v")]
		     UNSPEC_EXTENDDITI2))]
  "TARGET_POWER10"
  "vextsd2q %0,%1"
  [(set_attr "type" "vecexts")])


;; ISA 3.0 Binary Floating-Point Support

;; VSX Scalar Extract Exponent Quad-Precision
(define_insn "xsxexpqp_<mode>"
  [(set (match_operand:DI 0 "altivec_register_operand" "=v")
	(unspec:DI [(match_operand:IEEE128 1 "altivec_register_operand" "v")]
	 UNSPEC_VSX_SXEXPDP))]
  "TARGET_P9_VECTOR"
  "xsxexpqp %0,%1"
  [(set_attr "type" "vecmove")])

;; VSX Scalar Extract Exponent Double-Precision
(define_insn "xsxexpdp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DF 1 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_SXEXPDP))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "xsxexpdp %0,%x1"
  [(set_attr "type" "integer")])

;; VSX Scalar Extract Significand Quad-Precision
(define_insn "xsxsigqp_<mode>"
  [(set (match_operand:TI 0 "altivec_register_operand" "=v")
	(unspec:TI [(match_operand:IEEE128 1 "altivec_register_operand" "v")]
	 UNSPEC_VSX_SXSIG))]
  "TARGET_P9_VECTOR"
  "xsxsigqp %0,%1"
  [(set_attr "type" "vecmove")])

;; VSX Scalar Extract Significand Double-Precision
(define_insn "xsxsigdp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(match_operand:DF 1 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_SXSIG))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "xsxsigdp %0,%x1"
  [(set_attr "type" "integer")])

;; VSX Scalar Insert Exponent Quad-Precision Floating Point Argument
(define_insn "xsiexpqpf_<mode>"
  [(set (match_operand:IEEE128 0 "altivec_register_operand" "=v")
	(unspec:IEEE128
	 [(match_operand:IEEE128 1 "altivec_register_operand" "v")
	  (match_operand:DI 2 "altivec_register_operand" "v")]
	 UNSPEC_VSX_SIEXPQP))]
  "TARGET_P9_VECTOR"
  "xsiexpqp %0,%1,%2"
  [(set_attr "type" "vecmove")])

;; VSX Scalar Insert Exponent Quad-Precision
(define_insn "xsiexpqp_<mode>"
  [(set (match_operand:IEEE128 0 "altivec_register_operand" "=v")
	(unspec:IEEE128 [(match_operand:TI 1 "altivec_register_operand" "v")
			 (match_operand:DI 2 "altivec_register_operand" "v")]
	 UNSPEC_VSX_SIEXPQP))]
  "TARGET_P9_VECTOR"
  "xsiexpqp %0,%1,%2"
  [(set_attr "type" "vecmove")])

;; VSX Scalar Insert Exponent Double-Precision
(define_insn "xsiexpdp"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:DI 1 "register_operand" "r")
		    (match_operand:DI 2 "register_operand" "r")]
	 UNSPEC_VSX_SIEXPDP))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "xsiexpdp %x0,%1,%2"
  [(set_attr "type" "fpsimple")])

;; VSX Scalar Insert Exponent Double-Precision Floating Point Argument
(define_insn "xsiexpdpf"
  [(set (match_operand:DF 0 "vsx_register_operand" "=wa")
	(unspec:DF [(match_operand:DF 1 "register_operand" "r")
		    (match_operand:DI 2 "register_operand" "r")]
	 UNSPEC_VSX_SIEXPDP))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "xsiexpdp %x0,%1,%2"
  [(set_attr "type" "fpsimple")])

;; VSX Scalar Compare Exponents Double-Precision
(define_expand "xscmpexpdp_<code>"
  [(set (match_dup 3)
	(compare:CCFP
	 (unspec:DF
	  [(match_operand:DF 1 "vsx_register_operand" "wa")
	   (match_operand:DF 2 "vsx_register_operand" "wa")]
	  UNSPEC_VSX_SCMPEXPDP)
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(CMP_TEST:SI (match_dup 3)
		     (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  if (<CODE> == UNORDERED && !HONOR_NANS (DFmode))
    {
      emit_move_insn (operands[0], const0_rtx);
      DONE;
    }

  operands[3] = gen_reg_rtx (CCFPmode);
})

(define_insn "*xscmpexpdp"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(compare:CCFP
	 (unspec:DF [(match_operand:DF 1 "vsx_register_operand" "wa")
		     (match_operand:DF 2 "vsx_register_operand" "wa")]
	  UNSPEC_VSX_SCMPEXPDP)
	 (match_operand:SI 3 "zero_constant" "j")))]
  "TARGET_P9_VECTOR"
  "xscmpexpdp %0,%x1,%x2"
  [(set_attr "type" "fpcompare")])

;; VSX Scalar Compare Exponents Quad-Precision
(define_expand "xscmpexpqp_<code>_<mode>"
  [(set (match_dup 3)
	(compare:CCFP
	 (unspec:IEEE128
	  [(match_operand:IEEE128 1 "vsx_register_operand" "v")
	   (match_operand:IEEE128 2 "vsx_register_operand" "v")]
	  UNSPEC_VSX_SCMPEXPQP)
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(CMP_TEST:SI (match_dup 3)
		     (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  if (<CODE> == UNORDERED && !HONOR_NANS (<MODE>mode))
    {
      emit_move_insn (operands[0], const0_rtx);
      DONE;
    }

  operands[3] = gen_reg_rtx (CCFPmode);
})

(define_insn "*xscmpexpqp"
  [(set (match_operand:CCFP 0 "cc_reg_operand" "=y")
	(compare:CCFP
	 (unspec:IEEE128 [(match_operand:IEEE128 1 "altivec_register_operand" "v")
		          (match_operand:IEEE128 2 "altivec_register_operand" "v")]
	  UNSPEC_VSX_SCMPEXPQP)
	 (match_operand:SI 3 "zero_constant" "j")))]
  "TARGET_P9_VECTOR"
  "xscmpexpqp %0,%1,%2"
  [(set_attr "type" "fpcompare")])

;; VSX Scalar Test Data Class Quad-Precision
;;  (Expansion for scalar_test_data_class (__ieee128, int))
;;   (Has side effect of setting the lt bit if operand 1 is negative,
;;    setting the eq bit if any of the conditions tested by operand 2
;;    are satisfied, and clearing the gt and undordered bits to zero.)
(define_expand "xststdcqp_<mode>"
  [(set (match_dup 3)
	(compare:CCFP
	 (unspec:IEEE128
	  [(match_operand:IEEE128 1 "altivec_register_operand" "v")
	   (match_operand:SI 2 "u7bit_cint_operand" "n")]
	  UNSPEC_VSX_STSTDC)
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(eq:SI (match_dup 3)
	       (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  operands[3] = gen_reg_rtx (CCFPmode);
})

;; VSX Scalar Test Data Class Double- and Single-Precision
;;  (The lt bit is set if operand 1 is negative.  The eq bit is set
;;   if any of the conditions tested by operand 2 are satisfied.
;;   The gt and unordered bits are cleared to zero.)
(define_expand "xststdc<sd>p"
  [(set (match_dup 3)
	(compare:CCFP
	 (unspec:SFDF
	  [(match_operand:SFDF 1 "vsx_register_operand" "wa")
	   (match_operand:SI 2 "u7bit_cint_operand" "n")]
	  UNSPEC_VSX_STSTDC)
	 (match_dup 4)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(eq:SI (match_dup 3)
	       (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  operands[3] = gen_reg_rtx (CCFPmode);
  operands[4] = CONST0_RTX (SImode);
})

;; The VSX Scalar Test Negative Quad-Precision
(define_expand "xststdcnegqp_<mode>"
  [(set (match_dup 2)
	(compare:CCFP
	 (unspec:IEEE128
	  [(match_operand:IEEE128 1 "altivec_register_operand" "v")
	   (const_int 0)]
	  UNSPEC_VSX_STSTDC)
	 (const_int 0)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(lt:SI (match_dup 2)
	       (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  operands[2] = gen_reg_rtx (CCFPmode);
})

;; The VSX Scalar Test Negative Double- and Single-Precision
(define_expand "xststdcneg<sd>p"
  [(set (match_dup 2)
	(compare:CCFP
	 (unspec:SFDF
	  [(match_operand:SFDF 1 "vsx_register_operand" "wa")
	   (const_int 0)]
	  UNSPEC_VSX_STSTDC)
	 (match_dup 3)))
   (set (match_operand:SI 0 "register_operand" "=r")
	(lt:SI (match_dup 2)
	       (const_int 0)))]
  "TARGET_P9_VECTOR"
{
  operands[2] = gen_reg_rtx (CCFPmode);
  operands[3] = CONST0_RTX (SImode);
})

(define_insn "*xststdcqp_<mode>"
  [(set (match_operand:CCFP 0 "" "=y")
	(compare:CCFP
	 (unspec:IEEE128
	  [(match_operand:IEEE128 1 "altivec_register_operand" "v")
	   (match_operand:SI 2 "u7bit_cint_operand" "n")]
	  UNSPEC_VSX_STSTDC)
	 (const_int 0)))]
  "TARGET_P9_VECTOR"
  "xststdcqp %0,%1,%2"
  [(set_attr "type" "fpcompare")])

(define_insn "*xststdc<sd>p"
  [(set (match_operand:CCFP 0 "" "=y")
	(compare:CCFP
	 (unspec:SFDF [(match_operand:SFDF 1 "vsx_register_operand" "wa")
		       (match_operand:SI 2 "u7bit_cint_operand" "n")]
	  UNSPEC_VSX_STSTDC)
	 (match_operand:SI 3 "zero_constant" "j")))]
  "TARGET_P9_VECTOR"
  "xststdc<sd>p %0,%x1,%2"
  [(set_attr "type" "fpcompare")])

;; VSX Vector Extract Exponent Double and Single Precision
(define_insn "xvxexp<sd>p"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F
	 [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_VXEXP))]
  "TARGET_P9_VECTOR"
  "xvxexp<sd>p %x0,%x1"
  [(set_attr "type" "vecsimple")])

;; VSX Vector Extract Significand Double and Single Precision
(define_insn "xvxsig<sd>p"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F
	 [(match_operand:VSX_F 1 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_VXSIG))]
  "TARGET_P9_VECTOR"
  "xvxsig<sd>p %x0,%x1"
  [(set_attr "type" "vecsimple")])

;; VSX Vector Insert Exponent Double and Single Precision
(define_insn "xviexp<sd>p"
  [(set (match_operand:VSX_F 0 "vsx_register_operand" "=wa")
	(unspec:VSX_F
	 [(match_operand:VSX_F 1 "vsx_register_operand" "wa")
	  (match_operand:VSX_F 2 "vsx_register_operand" "wa")]
	 UNSPEC_VSX_VIEXP))]
  "TARGET_P9_VECTOR"
  "xviexp<sd>p %x0,%x1,%x2"
  [(set_attr "type" "vecsimple")])

;; VSX Vector Test Data Class Double and Single Precision
;; The corresponding elements of the result vector are all ones
;; if any of the conditions tested by operand 3 are satisfied.
(define_insn "xvtstdc<sd>p"
  [(set (match_operand:<VSI> 0 "vsx_register_operand" "=wa")
	(unspec:<VSI>
	 [(match_operand:VSX_F 1 "vsx_register_operand" "wa")
	  (match_operand:SI 2 "u7bit_cint_operand" "n")]
	 UNSPEC_VSX_VTSTDC))]
  "TARGET_P9_VECTOR"
  "xvtstdc<sd>p %x0,%x1,%2"
  [(set_attr "type" "vecsimple")])

;; ISA 3.0 String Operations Support

;; Compare vectors producing a vector result and a predicate, setting CR6
;; to indicate a combined status.  This pattern matches v16qi, v8hi, and
;; v4si modes.  It does not match v2df, v4sf, or v2di modes.  There's no
;; need to match v4sf, v2df, or v2di modes because those are expanded
;; to use Power8 instructions.
(define_insn "*vsx_ne_<mode>_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC
	 [(ne:CC (match_operand:VSX_EXTRACT_I 1 "gpc_reg_operand" "v")
		 (match_operand:VSX_EXTRACT_I 2 "gpc_reg_operand" "v"))]
	 UNSPEC_PREDICATE))
   (set (match_operand:VSX_EXTRACT_I 0 "gpc_reg_operand" "=v")
	(ne:VSX_EXTRACT_I (match_dup 1)
			  (match_dup 2)))]
  "TARGET_P9_VECTOR"
  "vcmpne<VSX_EXTRACT_WIDTH>. %0,%1,%2"
  [(set_attr "type" "vecsimple")])

(define_insn "*vector_nez_<mode>_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC [(unspec:VI
		     [(match_operand:VI 1 "gpc_reg_operand" "v")
		      (match_operand:VI 2 "gpc_reg_operand" "v")]
		     UNSPEC_NEZ_P)]
	 UNSPEC_PREDICATE))
   (set (match_operand:VI 0 "gpc_reg_operand" "=v")
	(unspec:VI [(match_dup 1)
		    (match_dup 2)]
	 UNSPEC_NEZ_P))]
  "TARGET_P9_VECTOR"
  "vcmpnez<VSX_EXTRACT_WIDTH>. %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Return first position of match between vectors using natural order
;; for both LE and BE execution modes.
(define_expand "first_match_index_<mode>"
  [(match_operand:SI 0 "register_operand")
   (unspec:SI [(match_operand:VSX_EXTRACT_I 1 "register_operand")
	       (match_operand:VSX_EXTRACT_I 2 "register_operand")]
  UNSPEC_VSX_FIRST_MATCH_INDEX)]
  "TARGET_P9_VECTOR"
{
  int sh;

  rtx cmp_result = gen_reg_rtx (<MODE>mode);
  rtx not_result = gen_reg_rtx (<MODE>mode);

  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmp_result, operands[1],
					    operands[2]));
  emit_insn (gen_one_cmpl<mode>2 (not_result, cmp_result));

  sh = GET_MODE_SIZE (GET_MODE_INNER (<MODE>mode)) / 2;

  if (<MODE>mode == V16QImode)
    {
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (operands[0], not_result));
      else
        emit_insn (gen_vclzlsbb_<mode> (operands[0], not_result));
    }
  else
    {
      rtx tmp = gen_reg_rtx (SImode);
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (tmp, not_result));
      else
        emit_insn (gen_vclzlsbb_<mode> (tmp, not_result));
      emit_insn (gen_lshrsi3 (operands[0], tmp, GEN_INT (sh)));
    }
  DONE;
})

;; Return first position of match between vectors or end of string (EOS) using
;; natural element order for both LE and BE execution modes.
(define_expand "first_match_or_eos_index_<mode>"
  [(match_operand:SI 0 "register_operand")
   (unspec: SI [(match_operand:VSX_EXTRACT_I 1 "register_operand")
   (match_operand:VSX_EXTRACT_I 2 "register_operand")]
  UNSPEC_VSX_FIRST_MATCH_EOS_INDEX)]
  "TARGET_P9_VECTOR"
{
  int sh;
  rtx cmpz1_result = gen_reg_rtx (<MODE>mode);
  rtx cmpz2_result = gen_reg_rtx (<MODE>mode);
  rtx cmpz_result = gen_reg_rtx (<MODE>mode);
  rtx and_result = gen_reg_rtx (<MODE>mode);
  rtx result = gen_reg_rtx (<MODE>mode);
  rtx vzero = gen_reg_rtx (<MODE>mode);

  /* Vector with zeros in elements that correspond to zeros in operands.  */
  emit_move_insn (vzero, CONST0_RTX (<MODE>mode));
  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmpz1_result, operands[1], vzero));
  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmpz2_result, operands[2], vzero));
  emit_insn (gen_and<mode>3 (and_result, cmpz1_result, cmpz2_result));

  /* Vector with ones in elments that do not match.  */
  emit_insn (gen_vcmpnez<VSX_EXTRACT_WIDTH> (cmpz_result, operands[1],
                                             operands[2]));

  /* Create vector with ones in elements where there was a zero in one of
     the source elements or the elements that match.  */
  emit_insn (gen_nand<mode>3 (result, and_result, cmpz_result));
  sh = GET_MODE_SIZE (GET_MODE_INNER (<MODE>mode)) / 2;

  if (<MODE>mode == V16QImode)
    {
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (operands[0], result));
      else
        emit_insn (gen_vclzlsbb_<mode> (operands[0], result));
    }
  else
    {
      rtx tmp = gen_reg_rtx (SImode);
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (tmp, result));
      else
        emit_insn (gen_vclzlsbb_<mode> (tmp, result));
      emit_insn (gen_lshrsi3 (operands[0], tmp, GEN_INT (sh)));
    }
  DONE;
})

;; Return first position of mismatch between vectors using natural
;; element order for both LE and BE execution modes.
(define_expand "first_mismatch_index_<mode>"
  [(match_operand:SI 0 "register_operand")
   (unspec: SI [(match_operand:VSX_EXTRACT_I 1 "register_operand")
   (match_operand:VSX_EXTRACT_I 2 "register_operand")]
  UNSPEC_VSX_FIRST_MISMATCH_INDEX)]
  "TARGET_P9_VECTOR"
{
  int sh;
  rtx cmp_result = gen_reg_rtx (<MODE>mode);

  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmp_result, operands[1],
					    operands[2]));
  sh = GET_MODE_SIZE (GET_MODE_INNER (<MODE>mode)) / 2;

  if (<MODE>mode == V16QImode)
    {
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (operands[0], cmp_result));
      else
        emit_insn (gen_vclzlsbb_<mode> (operands[0], cmp_result));
    }
  else
    {
      rtx tmp = gen_reg_rtx (SImode);
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (tmp, cmp_result));
      else
        emit_insn (gen_vclzlsbb_<mode> (tmp, cmp_result));
      emit_insn (gen_lshrsi3 (operands[0], tmp, GEN_INT (sh)));
    }
  DONE;
})

;; Return first position of mismatch between vectors or end of string (EOS)
;; using natural element order for both LE and BE execution modes.
(define_expand "first_mismatch_or_eos_index_<mode>"
  [(match_operand:SI 0 "register_operand")
   (unspec: SI [(match_operand:VSX_EXTRACT_I 1 "register_operand")
   (match_operand:VSX_EXTRACT_I 2 "register_operand")]
  UNSPEC_VSX_FIRST_MISMATCH_EOS_INDEX)]
  "TARGET_P9_VECTOR"
{
  int sh;
  rtx cmpz1_result = gen_reg_rtx (<MODE>mode);
  rtx cmpz2_result = gen_reg_rtx (<MODE>mode);
  rtx cmpz_result = gen_reg_rtx (<MODE>mode);
  rtx not_cmpz_result = gen_reg_rtx (<MODE>mode);
  rtx and_result = gen_reg_rtx (<MODE>mode);
  rtx result = gen_reg_rtx (<MODE>mode);
  rtx vzero = gen_reg_rtx (<MODE>mode);

  /* Vector with zeros in elements that correspond to zeros in operands.  */
  emit_move_insn (vzero, CONST0_RTX (<MODE>mode));

  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmpz1_result, operands[1], vzero));
  emit_insn (gen_vcmpne<VSX_EXTRACT_WIDTH> (cmpz2_result, operands[2], vzero));
  emit_insn (gen_and<mode>3 (and_result, cmpz1_result, cmpz2_result));

  /* Vector with ones in elments that match.  */
  emit_insn (gen_vcmpnez<VSX_EXTRACT_WIDTH> (cmpz_result, operands[1],
                                             operands[2]));
  emit_insn (gen_one_cmpl<mode>2 (not_cmpz_result, cmpz_result));

  /* Create vector with ones in elements where there was a zero in one of
     the source elements or the elements did not match.  */
  emit_insn (gen_nand<mode>3 (result, and_result, not_cmpz_result));
  sh = GET_MODE_SIZE (GET_MODE_INNER (<MODE>mode)) / 2;

  if (<MODE>mode == V16QImode)
    {
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (operands[0], result));
      else
        emit_insn (gen_vclzlsbb_<mode> (operands[0], result));
    }
  else
    {
      rtx tmp = gen_reg_rtx (SImode);
      if (!BYTES_BIG_ENDIAN)
        emit_insn (gen_vctzlsbb_<mode> (tmp, result));
      else
        emit_insn (gen_vclzlsbb_<mode> (tmp, result));
      emit_insn (gen_lshrsi3 (operands[0], tmp, GEN_INT (sh)));
    }
  DONE;
})

;; Load VSX Vector with Length
(define_expand "lxvl"
  [(set (match_dup 3)
        (ashift:DI (match_operand:DI 2 "register_operand")
                   (const_int 56)))
   (set (match_operand:V16QI 0 "vsx_register_operand")
	(unspec:V16QI
	 [(match_operand:DI 1 "gpc_reg_operand")
          (mem:V16QI (match_dup 1))
	  (match_dup 3)]
	 UNSPEC_LXVL))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
{
  operands[3] = gen_reg_rtx (DImode);
})

(define_insn "*lxvl"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(unspec:V16QI
	 [(match_operand:DI 1 "gpc_reg_operand" "b")
	  (mem:V16QI (match_dup 1))
	  (match_operand:DI 2 "register_operand" "r")]
	 UNSPEC_LXVL))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "lxvl %x0,%1,%2"
  [(set_attr "type" "vecload")])

(define_insn "lxvll"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(unspec:V16QI [(match_operand:DI 1 "gpc_reg_operand" "b")
                       (mem:V16QI (match_dup 1))
		       (match_operand:DI 2 "register_operand" "r")]
		      UNSPEC_LXVLL))]
  "TARGET_P9_VECTOR"
  "lxvll %x0,%1,%2"
  [(set_attr "type" "vecload")])

;; Expand for builtin xl_len_r
(define_expand "xl_len_r"
  [(match_operand:V16QI 0 "vsx_register_operand")
   (match_operand:DI 1 "register_operand")
   (match_operand:DI 2 "register_operand")]
  ""
{
  rtx shift_mask = gen_reg_rtx (V16QImode);
  rtx rtx_vtmp = gen_reg_rtx (V16QImode);
  rtx tmp = gen_reg_rtx (DImode);

  emit_insn (gen_altivec_lvsl_reg_di (shift_mask, operands[2]));
  emit_insn (gen_ashldi3 (tmp, operands[2], GEN_INT (56)));
  emit_insn (gen_lxvll (rtx_vtmp, operands[1], tmp));
  emit_insn (gen_altivec_vperm_v8hiv16qi (operands[0], rtx_vtmp, rtx_vtmp,
	     shift_mask));
  DONE;
})

(define_insn "stxvll"
  [(set (mem:V16QI (match_operand:DI 1 "gpc_reg_operand" "b"))
	(unspec:V16QI [(match_operand:V16QI 0 "vsx_register_operand" "wa")
		       (mem:V16QI (match_dup 1))
		       (match_operand:DI 2 "register_operand" "r")]
	              UNSPEC_STXVLL))]
  "TARGET_P9_VECTOR"
  "stxvll %x0,%1,%2"
  [(set_attr "type" "vecstore")])

;; Store VSX Vector with Length
(define_expand "stxvl"
  [(set (match_dup 3)
	(ashift:DI (match_operand:DI 2 "register_operand")
		   (const_int 56)))
   (set (mem:V16QI (match_operand:DI 1 "gpc_reg_operand"))
	(unspec:V16QI
	 [(match_operand:V16QI 0 "vsx_register_operand")
	  (mem:V16QI (match_dup 1))
	  (match_dup 3)]
	 UNSPEC_STXVL))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
{
  operands[3] = gen_reg_rtx (DImode);
})

;; Define optab for vector access with length vectorization exploitation.
(define_expand "len_load_v16qi"
  [(match_operand:V16QI 0 "vlogical_operand")
   (match_operand:V16QI 1 "memory_operand")
   (match_operand:QI 2 "gpc_reg_operand")
   (match_operand:QI 3 "zero_constant")]
  "TARGET_P9_VECTOR && TARGET_64BIT"
{
  rtx mem = XEXP (operands[1], 0);
  mem = force_reg (DImode, mem);
  rtx len = gen_lowpart (DImode, operands[2]);
  emit_insn (gen_lxvl (operands[0], mem, len));
  DONE;
})

(define_expand "len_store_v16qi"
  [(match_operand:V16QI 0 "memory_operand")
   (match_operand:V16QI 1 "vlogical_operand")
   (match_operand:QI 2 "gpc_reg_operand")
   (match_operand:QI 3 "zero_constant")
  ]
  "TARGET_P9_VECTOR && TARGET_64BIT"
{
  rtx mem = XEXP (operands[0], 0);
  mem = force_reg (DImode, mem);
  rtx len = gen_lowpart (DImode, operands[2]);
  emit_insn (gen_stxvl (operands[1], mem, len));
  DONE;
})

(define_insn "*stxvl"
  [(set (mem:V16QI (match_operand:DI 1 "gpc_reg_operand" "b"))
	(unspec:V16QI
	 [(match_operand:V16QI 0 "vsx_register_operand" "wa")
	  (mem:V16QI (match_dup 1))
	  (match_operand:DI 2 "register_operand" "r")]
	 UNSPEC_STXVL))]
  "TARGET_P9_VECTOR && TARGET_64BIT"
  "stxvl %x0,%1,%2"
  [(set_attr "type" "vecstore")])

;; Expand for builtin xst_len_r
(define_expand "xst_len_r"
  [(match_operand:V16QI 0 "vsx_register_operand" "=wa")
   (match_operand:DI 1 "register_operand" "b")
   (match_operand:DI 2 "register_operand" "r")]
  "UNSPEC_XST_LEN_R"
{
  rtx shift_mask = gen_reg_rtx (V16QImode);
  rtx rtx_vtmp = gen_reg_rtx (V16QImode);
  rtx tmp = gen_reg_rtx (DImode);

  emit_insn (gen_altivec_lvsr_reg_di (shift_mask, operands[2]));
  emit_insn (gen_altivec_vperm_v8hiv16qi (rtx_vtmp, operands[0], operands[0],
	     shift_mask));
  emit_insn (gen_ashldi3 (tmp, operands[2], GEN_INT (56)));
  emit_insn (gen_stxvll (rtx_vtmp, operands[1], tmp));
  DONE;
})

;; Vector Compare Not Equal Byte (specified/not+eq:)
(define_insn "vcmpneb"
  [(set (match_operand:V16QI 0 "altivec_register_operand" "=v")
	 (not:V16QI
	   (eq:V16QI (match_operand:V16QI 1 "altivec_register_operand" "v")
		     (match_operand:V16QI 2 "altivec_register_operand" "v"))))]
  "TARGET_P9_VECTOR"
  "vcmpneb %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal v1ti (specified/not+eq:)
(define_expand "vcmpnet"
  [(set (match_operand:V1TI 0 "altivec_register_operand")
	(not:V1TI
	  (eq:V1TI (match_operand:V1TI 1 "altivec_register_operand")
		   (match_operand:V1TI 2 "altivec_register_operand"))))]
   "TARGET_POWER10"
{
  emit_insn (gen_eqvv1ti3 (operands[0], operands[1], operands[2]));
  emit_insn (gen_one_cmplv1ti2 (operands[0], operands[0]));
  DONE;
})

;; Vector Compare Not Equal or Zero Byte
(define_insn "vcmpnezb"
  [(set (match_operand:V16QI 0 "altivec_register_operand" "=v")
	(unspec:V16QI
	 [(match_operand:V16QI 1 "altivec_register_operand" "v")
	  (match_operand:V16QI 2 "altivec_register_operand" "v")]
	 UNSPEC_VCMPNEZB))]
  "TARGET_P9_VECTOR"
  "vcmpnezb %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal or Zero Byte predicate or record-form
(define_insn "vcmpnezb_p"
  [(set (reg:CC CR6_REGNO)
	(unspec:CC
	 [(match_operand:V16QI 1 "altivec_register_operand" "v")
	  (match_operand:V16QI 2 "altivec_register_operand" "v")]
	 UNSPEC_VCMPNEZB))
   (set (match_operand:V16QI 0 "altivec_register_operand" "=v")
	(unspec:V16QI
	 [(match_dup 1)
	  (match_dup 2)]
	 UNSPEC_VCMPNEZB))]
  "TARGET_P9_VECTOR"
  "vcmpnezb. %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal Half Word (specified/not+eq:)
(define_insn "vcmpneh"
  [(set (match_operand:V8HI 0 "altivec_register_operand" "=v")
	(not:V8HI
	  (eq:V8HI (match_operand:V8HI 1 "altivec_register_operand" "v")
		   (match_operand:V8HI 2 "altivec_register_operand" "v"))))]
  "TARGET_P9_VECTOR"
  "vcmpneh %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal or Zero Half Word
(define_insn "vcmpnezh"
  [(set (match_operand:V8HI 0 "altivec_register_operand" "=v")
	(unspec:V8HI [(match_operand:V8HI 1 "altivec_register_operand" "v")
		      (match_operand:V8HI 2 "altivec_register_operand" "v")]
	 UNSPEC_VCMPNEZH))]
  "TARGET_P9_VECTOR"
  "vcmpnezh %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal Word (specified/not+eq:)
(define_insn "vcmpnew"
  [(set (match_operand:V4SI 0 "altivec_register_operand" "=v")
	(not:V4SI
	  (eq:V4SI (match_operand:V4SI 1 "altivec_register_operand" "v")
		   (match_operand:V4SI 2 "altivec_register_operand" "v"))))]
  "TARGET_P9_VECTOR"
  "vcmpnew %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Compare Not Equal or Zero Word
(define_insn "vcmpnezw"
  [(set (match_operand:V4SI 0 "altivec_register_operand" "=v")
	(unspec:V4SI [(match_operand:V4SI 1 "altivec_register_operand" "v")
		      (match_operand:V4SI 2 "altivec_register_operand" "v")]
	 UNSPEC_VCMPNEZW))]
  "TARGET_P9_VECTOR"
  "vcmpnezw %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Count Leading Zero Least-Significant Bits Byte
(define_insn "vclzlsbb_<mode>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:VSX_EXTRACT_I 1 "altivec_register_operand" "v")]
	 UNSPEC_VCLZLSBB))]
  "TARGET_P9_VECTOR"
  "vclzlsbb %0,%1"
  [(set_attr "type" "vecsimple")])

;; Vector Count Trailing Zero Least-Significant Bits Byte
(define_insn "vctzlsbb_<mode>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:VSX_EXTRACT_I 1 "altivec_register_operand" "v")]
	 UNSPEC_VCTZLSBB))]
  "TARGET_P9_VECTOR"
  "vctzlsbb %0,%1"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Byte Left-Indexed
(define_insn "vextublx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V16QI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUBLX))]
  "TARGET_P9_VECTOR"
  "vextublx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Byte Right-Indexed
(define_insn "vextubrx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V16QI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUBRX))]
  "TARGET_P9_VECTOR"
  "vextubrx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Half Word Left-Indexed
(define_insn "vextuhlx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V8HI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUHLX))]
  "TARGET_P9_VECTOR"
  "vextuhlx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Half Word Right-Indexed
(define_insn "vextuhrx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V8HI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUHRX))]
  "TARGET_P9_VECTOR"
  "vextuhrx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Word Left-Indexed
(define_insn "vextuwlx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V4SI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUWLX))]
  "TARGET_P9_VECTOR"
  "vextuwlx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector Extract Unsigned Word Right-Indexed
(define_insn "vextuwrx"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI
	 [(match_operand:SI 1 "register_operand" "r")
	  (match_operand:V4SI 2 "altivec_register_operand" "v")]
	 UNSPEC_VEXTUWRX))]
  "TARGET_P9_VECTOR"
  "vextuwrx %0,%1,%2"
  [(set_attr "type" "vecsimple")])

;; Vector insert/extract word at arbitrary byte values.  Note, the little
;; endian version needs to adjust the byte number, and the V4SI element in
;; vinsert4b.
(define_insn "extract4b"
  [(set (match_operand:V2DI 0 "vsx_register_operand")
       (unspec:V2DI [(match_operand:V16QI 1 "vsx_register_operand" "wa")
                     (match_operand:QI 2 "const_0_to_12_operand" "n")]
                    UNSPEC_XXEXTRACTUW))]
  "TARGET_P9_VECTOR"
{
  if (!BYTES_BIG_ENDIAN)
    operands[2] = GEN_INT (12 - INTVAL (operands[2]));

  return "xxextractuw %x0,%x1,%2";
})

(define_expand "insert4b"
  [(set (match_operand:V16QI 0 "vsx_register_operand")
	(unspec:V16QI [(match_operand:V4SI 1 "vsx_register_operand")
		       (match_operand:V16QI 2 "vsx_register_operand")
		       (match_operand:QI 3 "const_0_to_12_operand")]
		   UNSPEC_XXINSERTW))]
  "TARGET_P9_VECTOR"
{
  if (!BYTES_BIG_ENDIAN)
    {
      rtx op1 = operands[1];
      rtx v4si_tmp = gen_reg_rtx (V4SImode);
      emit_insn (gen_vsx_xxpermdi_v4si_be (v4si_tmp, op1, op1, const1_rtx));
      operands[1] = v4si_tmp;
      operands[3] = GEN_INT (12 - INTVAL (operands[3]));
    }
})

(define_insn "*insert4b_internal"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(unspec:V16QI [(match_operand:V4SI 1 "vsx_register_operand" "wa")
		       (match_operand:V16QI 2 "vsx_register_operand" "0")
		       (match_operand:QI 3 "const_0_to_12_operand" "n")]
		   UNSPEC_XXINSERTW))]
  "TARGET_P9_VECTOR"
  "xxinsertw %x0,%x1,%3"
  [(set_attr "type" "vecperm")])


;; Generate vector extract four float 32 values from left four elements
;; of eight element vector of float 16 values.
(define_expand "vextract_fp_from_shorth"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:V8HI 1 "register_operand" "wa")]
   UNSPEC_VSX_VEXTRACT_FP_FROM_SHORTH))]
  "TARGET_P9_VECTOR"
{
  int i;
  int vals_le[16] = {15, 14, 0, 0, 13, 12, 0, 0, 11, 10, 0, 0, 9, 8, 0, 0};
  int vals_be[16] = {0, 0, 0, 1, 0, 0, 2, 3, 0, 0, 4, 5, 0, 0, 6, 7};

  rtx rvals[16];
  rtx mask = gen_reg_rtx (V16QImode);
  rtx tmp = gen_reg_rtx (V16QImode);
  rtvec v;

  for (i = 0; i < 16; i++)
    if (!BYTES_BIG_ENDIAN)
      rvals[i] = GEN_INT (vals_le[i]);
    else
      rvals[i] = GEN_INT (vals_be[i]);

  /* xvcvhpsp - vector convert F16 to vector F32 requires the four F16
     inputs in half words 1,3,5,7 (IBM numbering).  Use xxperm to move
     src half words 0,1,2,3 (LE), src half words 4,5,6,7 (BE) for the
     conversion instruction.  */
  v = gen_rtvec_v (16, rvals);
  emit_insn (gen_vec_initv16qiqi (mask, gen_rtx_PARALLEL (V16QImode, v)));
  emit_insn (gen_altivec_vperm_v8hiv16qi (tmp, operands[1],
					  operands[1], mask));
  emit_insn (gen_vsx_xvcvhpsp (operands[0], tmp));
  DONE;
})

;; Generate vector extract four float 32 values from right four elements
;; of eight element vector of float 16 values.
(define_expand "vextract_fp_from_shortl"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:V8HI 1 "register_operand" "wa")]
	UNSPEC_VSX_VEXTRACT_FP_FROM_SHORTL))]
  "TARGET_P9_VECTOR"
{
  int vals_le[16] = {7, 6, 0, 0, 5, 4, 0, 0, 3, 2, 0, 0, 1, 0, 0, 0};
  int vals_be[16] = {0, 0, 8, 9, 0, 0, 10, 11, 0, 0, 12, 13, 0, 0, 14, 15};

  int i;
  rtx rvals[16];
  rtx mask = gen_reg_rtx (V16QImode);
  rtx tmp = gen_reg_rtx (V16QImode);
  rtvec v;

  for (i = 0; i < 16; i++)
    if (!BYTES_BIG_ENDIAN)
      rvals[i] = GEN_INT (vals_le[i]);
    else
      rvals[i] = GEN_INT (vals_be[i]);

  /* xvcvhpsp - vector convert F16 to vector F32 requires the four F16
     inputs in half words 1,3,5,7 (IBM numbering).  Use xxperm to move
     src half words 4,5,6,7 (LE), src half words 0,1,2,3 (BE) for the
     conversion instruction.  */
  v = gen_rtvec_v (16, rvals);
  emit_insn (gen_vec_initv16qiqi (mask, gen_rtx_PARALLEL (V16QImode, v)));
  emit_insn (gen_altivec_vperm_v8hiv16qi (tmp, operands[1],
					  operands[1], mask));
  emit_insn (gen_vsx_xvcvhpsp (operands[0], tmp));
  DONE;
})

;; Support for ISA 3.0 vector byte reverse

;; Swap all bytes with in a vector
(define_insn "p9_xxbrq_v1ti"
  [(set (match_operand:V1TI 0 "vsx_register_operand" "=wa")
	(bswap:V1TI (match_operand:V1TI 1 "vsx_register_operand" "wa")))]
  "TARGET_P9_VECTOR"
  "xxbrq %x0,%x1"
  [(set_attr "type" "vecperm")])

(define_expand "p9_xxbrq_v16qi"
  [(use (match_operand:V16QI 0 "vsx_register_operand" "=wa"))
   (use (match_operand:V16QI 1 "vsx_register_operand" "wa"))]
  "TARGET_P9_VECTOR"
{
  rtx op0 = gen_reg_rtx (V1TImode);
  rtx op1 = gen_lowpart (V1TImode, operands[1]);
  emit_insn (gen_p9_xxbrq_v1ti (op0, op1));
  emit_move_insn (operands[0], gen_lowpart (V16QImode, op0));
  DONE;
})

;; Swap all bytes in each 64-bit element
(define_insn "p9_xxbrd_v2di"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=wa")
	(bswap:V2DI (match_operand:V2DI 1 "vsx_register_operand" "wa")))]
  "TARGET_P9_VECTOR"
  "xxbrd %x0,%x1"
  [(set_attr "type" "vecperm")])

(define_expand "p9_xxbrd_v2df"
  [(use (match_operand:V2DF 0 "vsx_register_operand" "=wa"))
   (use (match_operand:V2DF 1 "vsx_register_operand" "wa"))]
  "TARGET_P9_VECTOR"
{
  rtx op0 = gen_reg_rtx (V2DImode);
  rtx op1 = gen_lowpart (V2DImode, operands[1]);
  emit_insn (gen_p9_xxbrd_v2di (op0, op1));
  emit_move_insn (operands[0], gen_lowpart (V2DFmode, op0));
  DONE;
})

;; Swap all bytes in each 32-bit element
(define_insn "p9_xxbrw_v4si"
  [(set (match_operand:V4SI 0 "vsx_register_operand" "=wa")
	(bswap:V4SI (match_operand:V4SI 1 "vsx_register_operand" "wa")))]
  "TARGET_P9_VECTOR"
  "xxbrw %x0,%x1"
  [(set_attr "type" "vecperm")])

(define_expand "p9_xxbrw_v4sf"
  [(use (match_operand:V4SF 0 "vsx_register_operand" "=wa"))
   (use (match_operand:V4SF 1 "vsx_register_operand" "wa"))]
  "TARGET_P9_VECTOR"
{
  rtx op0 = gen_reg_rtx (V4SImode);
  rtx op1 = gen_lowpart (V4SImode, operands[1]);
  emit_insn (gen_p9_xxbrw_v4si (op0, op1));
  emit_move_insn (operands[0], gen_lowpart (V4SFmode, op0));
  DONE;
})

;; Swap all bytes in each element of vector
(define_expand "revb_<mode>"
  [(use (match_operand:VEC_REVB 0 "vsx_register_operand"))
   (use (match_operand:VEC_REVB 1 "vsx_register_operand"))]
  ""
{
  if (TARGET_P9_VECTOR)
    emit_insn (gen_p9_xxbr<VSX_XXBR>_<mode> (operands[0], operands[1]));
  else
    {
      /* Want to have the elements in reverse order relative
	 to the endian mode in use, i.e. in LE mode, put elements
	 in BE order.  */
      rtx sel = swap_endian_selector_for_mode(<MODE>mode);
      emit_insn (gen_altivec_vperm_<mode> (operands[0], operands[1],
					   operands[1], sel));
    }

  DONE;
})

;; Reversing bytes in vector char is just a NOP.
(define_expand "revb_v16qi"
  [(set (match_operand:V16QI 0 "vsx_register_operand")
	(bswap:V16QI (match_operand:V16QI 1 "vsx_register_operand")))]
  ""
{
  emit_move_insn (operands[0], operands[1]);
  DONE;
})

;; Swap all bytes in each 16-bit element
(define_insn "p9_xxbrh_v8hi"
  [(set (match_operand:V8HI 0 "vsx_register_operand" "=wa")
	(bswap:V8HI (match_operand:V8HI 1 "vsx_register_operand" "wa")))]
  "TARGET_P9_VECTOR"
  "xxbrh %x0,%x1"
  [(set_attr "type" "vecperm")])


;; Operand numbers for the following peephole2
(define_constants
  [(SFBOOL_TMP_GPR		 0)		;; GPR temporary
   (SFBOOL_TMP_VSX		 1)		;; vector temporary
   (SFBOOL_MFVSR_D		 2)		;; move to gpr dest
   (SFBOOL_MFVSR_A		 3)		;; move to gpr src
   (SFBOOL_BOOL_D		 4)		;; and/ior/xor dest
   (SFBOOL_BOOL_A1		 5)		;; and/ior/xor arg1
   (SFBOOL_BOOL_A2		 6)		;; and/ior/xor arg1
   (SFBOOL_SHL_D		 7)		;; shift left dest
   (SFBOOL_SHL_A		 8)		;; shift left arg
   (SFBOOL_MTVSR_D		 9)		;; move to vecter dest
   (SFBOOL_MFVSR_A_V4SF		10)		;; SFBOOL_MFVSR_A as V4SFmode
   (SFBOOL_BOOL_A_DI		11)		;; SFBOOL_BOOL_A1/A2 as DImode
   (SFBOOL_TMP_VSX_DI		12)		;; SFBOOL_TMP_VSX as DImode
   (SFBOOL_MTVSR_D_V4SF		13)])		;; SFBOOL_MTVSRD_D as V4SFmode

;; Attempt to optimize some common GLIBC operations using logical operations to
;; pick apart SFmode operations.  For example, there is code from e_powf.c
;; after macro expansion that looks like:
;;
;;	typedef union {
;;	  float value;
;;	  uint32_t word;
;;	} ieee_float_shape_type;
;;
;;	float t1;
;;	int32_t is;
;;
;;	do {
;;	  ieee_float_shape_type gf_u;
;;	  gf_u.value = (t1);
;;	  (is) = gf_u.word;
;;	} while (0);
;;
;;	do {
;;	  ieee_float_shape_type sf_u;
;;	  sf_u.word = (is & 0xfffff000);
;;	  (t1) = sf_u.value;
;;	} while (0);
;;
;;
;; This would result in two direct move operations (convert to memory format,
;; direct move to GPR, do the AND operation, direct move to VSX, convert to
;; scalar format).  With this peephole, we eliminate the direct move to the
;; GPR, and instead move the integer mask value to the vector register after a
;; shift and do the VSX logical operation.

;; The insns for dealing with SFmode in GPR registers looks like:
;; (set (reg:V4SF reg2) (unspec:V4SF [(reg:SF reg1)] UNSPEC_VSX_CVDPSPN))
;;
;; (set (reg:DI reg3) (unspec:DI [(reg:V4SF reg2)] UNSPEC_P8V_RELOAD_FROM_VSX))
;;
;; (set (reg:DI reg4) (and:DI (reg:DI reg3) (reg:DI reg3)))
;;
;; (set (reg:DI reg5) (ashift:DI (reg:DI reg4) (const_int 32)))
;;
;; (set (reg:SF reg6) (unspec:SF [(reg:DI reg5)] UNSPEC_P8V_MTVSRD))
;;
;; (set (reg:SF reg6) (unspec:SF [(reg:SF reg6)] UNSPEC_VSX_CVSPDPN))

(define_peephole2
  [(match_scratch:DI SFBOOL_TMP_GPR "r")
   (match_scratch:V4SF SFBOOL_TMP_VSX "wa")

   ;; MFVSRWZ (aka zero_extend)
   (set (match_operand:DI SFBOOL_MFVSR_D "int_reg_operand")
	(zero_extend:DI
	 (match_operand:SI SFBOOL_MFVSR_A "vsx_register_operand")))

   ;; AND/IOR/XOR operation on int
   (set (match_operand:SI SFBOOL_BOOL_D "int_reg_operand")
	(and_ior_xor:SI (match_operand:SI SFBOOL_BOOL_A1 "int_reg_operand")
			(match_operand:SI SFBOOL_BOOL_A2 "reg_or_cint_operand")))

   ;; SLDI
   (set (match_operand:DI SFBOOL_SHL_D "int_reg_operand")
	(ashift:DI (match_operand:DI SFBOOL_SHL_A "int_reg_operand")
		   (const_int 32)))

   ;; MTVSRD
   (set (match_operand:SF SFBOOL_MTVSR_D "vsx_register_operand")
	(unspec:SF [(match_dup SFBOOL_SHL_D)] UNSPEC_P8V_MTVSRD))]

  "TARGET_POWERPC64 && TARGET_DIRECT_MOVE
   /* The REG_P (xxx) tests prevents SUBREG's, which allows us to use REGNO
      to compare registers, when the mode is different.  */
   && REG_P (operands[SFBOOL_MFVSR_D]) && REG_P (operands[SFBOOL_BOOL_D])
   && REG_P (operands[SFBOOL_BOOL_A1]) && REG_P (operands[SFBOOL_SHL_D])
   && REG_P (operands[SFBOOL_SHL_A])   && REG_P (operands[SFBOOL_MTVSR_D])
   && (REG_P (operands[SFBOOL_BOOL_A2])
       || CONST_INT_P (operands[SFBOOL_BOOL_A2]))
   && (REGNO (operands[SFBOOL_BOOL_D]) == REGNO (operands[SFBOOL_MFVSR_D])
       || peep2_reg_dead_p (2, operands[SFBOOL_MFVSR_D]))
   && (REGNO (operands[SFBOOL_MFVSR_D]) == REGNO (operands[SFBOOL_BOOL_A1])
       || (REG_P (operands[SFBOOL_BOOL_A2])
	   && REGNO (operands[SFBOOL_MFVSR_D])
		== REGNO (operands[SFBOOL_BOOL_A2])))
   && REGNO (operands[SFBOOL_BOOL_D]) == REGNO (operands[SFBOOL_SHL_A])
   && (REGNO (operands[SFBOOL_SHL_D]) == REGNO (operands[SFBOOL_BOOL_D])
       || peep2_reg_dead_p (3, operands[SFBOOL_BOOL_D]))
   && peep2_reg_dead_p (4, operands[SFBOOL_SHL_D])"
  [(set (match_dup SFBOOL_TMP_GPR)
	(ashift:DI (match_dup SFBOOL_BOOL_A_DI)
		   (const_int 32)))

   (set (match_dup SFBOOL_TMP_VSX_DI)
	(match_dup SFBOOL_TMP_GPR))

   (set (match_dup SFBOOL_MTVSR_D_V4SF)
	(and_ior_xor:V4SF (match_dup SFBOOL_MFVSR_A_V4SF)
			  (match_dup SFBOOL_TMP_VSX)))]
{
  rtx bool_a1 = operands[SFBOOL_BOOL_A1];
  rtx bool_a2 = operands[SFBOOL_BOOL_A2];
  int regno_mfvsr_d = REGNO (operands[SFBOOL_MFVSR_D]);
  int regno_mfvsr_a = REGNO (operands[SFBOOL_MFVSR_A]);
  int regno_tmp_vsx = REGNO (operands[SFBOOL_TMP_VSX]);
  int regno_mtvsr_d = REGNO (operands[SFBOOL_MTVSR_D]);

  if (CONST_INT_P (bool_a2))
    {
      rtx tmp_gpr = operands[SFBOOL_TMP_GPR];
      emit_move_insn (tmp_gpr, bool_a2);
      operands[SFBOOL_BOOL_A_DI] = tmp_gpr;
    }
  else
    {
      int regno_bool_a1 = REGNO (bool_a1);
      int regno_bool_a2 = REGNO (bool_a2);
      int regno_bool_a = (regno_mfvsr_d == regno_bool_a1
			  ? regno_bool_a2 : regno_bool_a1);
      operands[SFBOOL_BOOL_A_DI] = gen_rtx_REG (DImode, regno_bool_a);
    }

  operands[SFBOOL_MFVSR_A_V4SF] = gen_rtx_REG (V4SFmode, regno_mfvsr_a);
  operands[SFBOOL_TMP_VSX_DI] = gen_rtx_REG (DImode, regno_tmp_vsx);
  operands[SFBOOL_MTVSR_D_V4SF] = gen_rtx_REG (V4SFmode, regno_mtvsr_d);
})

;; Support signed/unsigned long long to float conversion vectorization.
;; Note that any_float (pc) here is just for code attribute <su>.
(define_expand "vec_pack<su>_float_v2di"
  [(match_operand:V4SF 0 "vfloat_operand")
   (match_operand:V2DI 1 "vint_operand")
   (match_operand:V2DI 2 "vint_operand")
   (any_float (pc))]
  "TARGET_VSX"
{
  rtx r1 = gen_reg_rtx (V4SFmode);
  rtx r2 = gen_reg_rtx (V4SFmode);
  emit_insn (gen_vsx_xvcv<su>xdsp (r1, operands[1]));
  emit_insn (gen_vsx_xvcv<su>xdsp (r2, operands[2]));
  rs6000_expand_extract_even (operands[0], r1, r2);
  DONE;
})

;; Support float to signed/unsigned long long conversion vectorization.
;; Note that any_fix (pc) here is just for code attribute <su>.
(define_expand "vec_unpack_<su>fix_trunc_hi_v4sf"
  [(match_operand:V2DI 0 "vint_operand")
   (match_operand:V4SF 1 "vfloat_operand")
   (any_fix (pc))]
  "TARGET_VSX"
{
  rtx reg = gen_reg_rtx (V4SFmode);
  rs6000_expand_interleave (reg, operands[1], operands[1], BYTES_BIG_ENDIAN);
  emit_insn (gen_vsx_xvcvsp<su>xds (operands[0], reg));
  DONE;
})

;; Note that any_fix (pc) here is just for code attribute <su>.
(define_expand "vec_unpack_<su>fix_trunc_lo_v4sf"
  [(match_operand:V2DI 0 "vint_operand")
   (match_operand:V4SF 1 "vfloat_operand")
   (any_fix (pc))]
  "TARGET_VSX"
{
  rtx reg = gen_reg_rtx (V4SFmode);
  rs6000_expand_interleave (reg, operands[1], operands[1], !BYTES_BIG_ENDIAN);
  emit_insn (gen_vsx_xvcvsp<su>xds (operands[0], reg));
  DONE;
})

(define_insn "vsx_<xvcvbf16>"
  [(set (match_operand:V16QI 0 "vsx_register_operand" "=wa")
	(unspec:V16QI [(match_operand:V16QI 1 "vsx_register_operand" "wa")]
		      XVCVBF16))]
  "TARGET_POWER10"
  "<xvcvbf16> %x0,%x1"
  [(set_attr "type" "vecfloat")])

(define_insn "vec_mtvsrbmi"
  [(set (match_operand:V16QI 0 "altivec_register_operand" "=v")
        (unspec:V16QI [(match_operand:QI 1 "u6bit_cint_operand" "n")]
        UNSPEC_MTVSBM))]
  "TARGET_POWER10"
  "mtvsrbmi %0,%1"
)

(define_insn "vec_mtvsr_<mode>"
  [(set (match_operand:VSX_MM 0 "altivec_register_operand" "=v")
        (unspec:VSX_MM [(match_operand:DI 1 "gpc_reg_operand" "r")]
        UNSPEC_MTVSBM))]
  "TARGET_POWER10"
  "mtvsr<wd>m %0,%1";
  [(set_attr "type" "vecsimple")])

(define_insn "vec_cntmb_<mode>"
  [(set (match_operand:DI 0 "gpc_reg_operand" "=r")
        (unspec:DI [(match_operand:VSX_MM4 1 "altivec_register_operand" "v")
                    (match_operand:QI 2 "const_0_to_1_operand" "n")]
        UNSPEC_VCNTMB))]
  "TARGET_POWER10"
  "vcntmb<wd> %0,%1,%2"
  [(set_attr "type" "vecsimple")])

(define_insn "vec_extract_<mode>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:VSX_MM 1 "altivec_register_operand" "v")]
	UNSPEC_VEXTRACT))]
  "TARGET_POWER10"
  "vextract<wd>m %0,%1"
  [(set_attr "type" "vecsimple")])

(define_insn "vec_expand_<mode>"
  [(set (match_operand:VSX_MM 0 "vsx_register_operand" "=v")
        (unspec:VSX_MM [(match_operand:VSX_MM 1 "vsx_register_operand" "v")]
        UNSPEC_VEXPAND))]
  "TARGET_POWER10"
  "vexpand<wd>m %0,%1"
  [(set_attr "type" "vecsimple")])

(define_insn "dives_<mode>"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
        (unspec:VIlong [(match_operand:VIlong 1 "vsx_register_operand" "v")
		        (match_operand:VIlong 2 "vsx_register_operand" "v")]
        UNSPEC_VDIVES))]
  "TARGET_POWER10"
  "vdives<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "diveu_<mode>"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
        (unspec:VIlong [(match_operand:VIlong 1 "vsx_register_operand" "v")
			(match_operand:VIlong 2 "vsx_register_operand" "v")]
	UNSPEC_VDIVEU))]
  "TARGET_POWER10"
  "vdiveu<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "div<mode>3"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(div:VIlong (match_operand:VIlong 1 "vsx_register_operand" "v")
		    (match_operand:VIlong 2 "vsx_register_operand" "v")))]
  "TARGET_POWER10"
  "vdivs<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "udiv<mode>3"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(udiv:VIlong (match_operand:VIlong 1 "vsx_register_operand" "v")
		    (match_operand:VIlong 2 "vsx_register_operand" "v")))]
  "TARGET_POWER10"
  "vdivu<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "mod<mode>3"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(mod:VIlong (match_operand:VIlong 1 "vsx_register_operand" "v")
		    (match_operand:VIlong 2 "vsx_register_operand" "v")))]
  "TARGET_POWER10"
  "vmods<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "umod<mode>3"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(umod:VIlong (match_operand:VIlong 1 "vsx_register_operand" "v")
		     (match_operand:VIlong 2 "vsx_register_operand" "v")))]
  "TARGET_POWER10"
  "vmodu<wd> %0,%1,%2"
  [(set_attr "type" "vecdiv")
   (set_attr "size" "<bits>")])

(define_insn "smul<mode>3_highpart"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(mult:VIlong (ashiftrt
		       (match_operand:VIlong 1 "vsx_register_operand" "v")
		       (const_int 32))
		     (ashiftrt
		       (match_operand:VIlong 2 "vsx_register_operand" "v")
		       (const_int 32))))]
  "TARGET_POWER10"
  "vmulhs<wd> %0,%1,%2"
  [(set_attr "type" "veccomplex")])

(define_insn "umul<mode>3_highpart"
  [(set (match_operand:VIlong 0 "vsx_register_operand" "=v")
	(us_mult:VIlong (ashiftrt
			  (match_operand:VIlong 1 "vsx_register_operand" "v")
			  (const_int 32))
			(ashiftrt
			  (match_operand:VIlong 2 "vsx_register_operand" "v")
			  (const_int 32))))]
  "TARGET_POWER10"
  "vmulhu<wd> %0,%1,%2"
  [(set_attr "type" "veccomplex")])

;; Vector multiply low double word
(define_insn "mulv2di3"
  [(set (match_operand:V2DI 0 "vsx_register_operand" "=v")
	(mult:V2DI (match_operand:V2DI 1 "vsx_register_operand" "v")
		   (match_operand:V2DI 2 "vsx_register_operand" "v")))]
  "TARGET_POWER10"
  "vmulld %0,%1,%2"
  [(set_attr "type" "veccomplex")])


;; XXSPLTIW built-in function support
(define_insn "xxspltiw_v4si"
  [(set (match_operand:V4SI 0 "register_operand" "=wa")
	(unspec:V4SI [(match_operand:SI 1 "s32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTIW))]
 "TARGET_POWER10"
 "xxspltiw %x0,%1"
 [(set_attr "type" "vecperm")
  (set_attr "prefixed" "yes")])

(define_expand "xxspltiw_v4sf"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:SF 1 "const_double_operand" "n")]
		     UNSPEC_XXSPLTIW))]
 "TARGET_POWER10"
{
  long value = rs6000_const_f32_to_i32 (operands[1]);
  emit_insn (gen_xxspltiw_v4sf_inst (operands[0], GEN_INT (value)));
  DONE;
})

(define_insn "xxspltiw_v4sf_inst"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:SI 1 "c32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTIW))]
 "TARGET_POWER10"
 "xxspltiw %x0,%1"
 [(set_attr "type" "vecperm")
  (set_attr "prefixed" "yes")])

;; XXSPLTIDP built-in function support
(define_expand "xxspltidp_v2df"
  [(set (match_operand:V2DF 0 "register_operand" )
	(unspec:V2DF [(match_operand:SF 1 "const_double_operand")]
		     UNSPEC_XXSPLTIDP))]
 "TARGET_POWER10"
{
  long value = rs6000_const_f32_to_i32 (operands[1]);
  rs6000_emit_xxspltidp_v2df (operands[0], value);
  DONE;
})

(define_insn "xxspltidp_v2df_inst"
  [(set (match_operand:V2DF 0 "register_operand" "=wa")
	(unspec:V2DF [(match_operand:SI 1 "c32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTIDP))]
  "TARGET_POWER10"
  "xxspltidp %x0,%1"
  [(set_attr "type" "vecperm")
   (set_attr "prefixed" "yes")])

;; XXSPLTI32DX built-in function support
(define_expand "xxsplti32dx_v4si"
  [(set (match_operand:V4SI 0 "register_operand" "=wa")
	(unspec:V4SI [(match_operand:V4SI 1 "register_operand" "0")
		      (match_operand:QI 2 "u1bit_cint_operand" "n")
		      (match_operand:SI 3 "s32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTI32DX))]
 "TARGET_POWER10"
{
  int index = INTVAL (operands[2]);

  if (!BYTES_BIG_ENDIAN)
    index = 1 - index;

   emit_insn (gen_xxsplti32dx_v4si_inst (operands[0], operands[1],
					 GEN_INT (index), operands[3]));
   DONE;
}
 [(set_attr "type" "vecperm")])

(define_insn "xxsplti32dx_v4si_inst"
  [(set (match_operand:V4SI 0 "register_operand" "=wa")
	(unspec:V4SI [(match_operand:V4SI 1 "register_operand" "0")
		      (match_operand:QI 2 "u1bit_cint_operand" "n")
		      (match_operand:SI 3 "s32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTI32DX))]
  "TARGET_POWER10"
  "xxsplti32dx %x0,%2,%3"
  [(set_attr "type" "vecperm")
   (set_attr "prefixed" "yes")])

(define_expand "xxsplti32dx_v4sf"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:V4SF 1 "register_operand" "0")
		      (match_operand:QI 2 "u1bit_cint_operand" "n")
		      (match_operand:SF 3 "const_double_operand" "n")]
		     UNSPEC_XXSPLTI32DX))]
  "TARGET_POWER10"
{
  int index = INTVAL (operands[2]);
  long value = rs6000_const_f32_to_i32 (operands[3]);
  if (!BYTES_BIG_ENDIAN)
    index = 1 - index;

   emit_insn (gen_xxsplti32dx_v4sf_inst (operands[0], operands[1],
					 GEN_INT (index), GEN_INT (value)));
   DONE;
})

(define_insn "xxsplti32dx_v4sf_inst"
  [(set (match_operand:V4SF 0 "register_operand" "=wa")
	(unspec:V4SF [(match_operand:V4SF 1 "register_operand" "0")
		      (match_operand:QI 2 "u1bit_cint_operand" "n")
		      (match_operand:SI 3 "s32bit_cint_operand" "n")]
		     UNSPEC_XXSPLTI32DX))]
  "TARGET_POWER10"
  "xxsplti32dx %x0,%2,%3"
  [(set_attr "type" "vecperm")
   (set_attr "prefixed" "yes")])

;; XXBLEND built-in function support
(define_insn "xxblend_<mode>"
  [(set (match_operand:VM3 0 "register_operand" "=wa")
	(unspec:VM3 [(match_operand:VM3 1 "register_operand" "wa")
		     (match_operand:VM3 2 "register_operand" "wa")
		     (match_operand:VM3 3 "register_operand" "wa")]
		    UNSPEC_XXBLEND))]
  "TARGET_POWER10"
  "xxblendv<VM3_char> %x0,%x1,%x2,%x3"
  [(set_attr "type" "vecperm")
   (set_attr "prefixed" "yes")])

;; XXPERMX built-in function support
(define_expand "xxpermx"
  [(set (match_operand:V2DI 0 "register_operand" "+wa")
	(unspec:V2DI [(match_operand:V2DI 1 "register_operand" "wa")
		      (match_operand:V2DI 2 "register_operand" "wa")
		      (match_operand:V16QI 3 "register_operand" "wa")
		      (match_operand:QI 4 "u8bit_cint_operand" "n")]
		     UNSPEC_XXPERMX))]
  "TARGET_POWER10"
{
  if (BYTES_BIG_ENDIAN)
    emit_insn (gen_xxpermx_inst (operands[0], operands[1],
				 operands[2], operands[3],
				 operands[4]));
  else
    {
      /* Reverse value of byte element indexes by XORing with 0xFF.
	 Reverse the 32-byte section identifier match by subracting bits [0:2]
	 of elemet from 7.  */
      int value = INTVAL (operands[4]);
      rtx vreg = gen_reg_rtx (V16QImode);

      emit_insn (gen_xxspltib_v16qi (vreg, GEN_INT (-1)));
      emit_insn (gen_xorv16qi3 (operands[3], operands[3], vreg));
      value = 7 - value;
      emit_insn (gen_xxpermx_inst (operands[0], operands[2],
				   operands[1], operands[3],
				   GEN_INT (value)));
    }

  DONE;
}
  [(set_attr "type" "vecperm")])

(define_insn "xxpermx_inst"
  [(set (match_operand:V2DI 0 "register_operand" "+v")
	(unspec:V2DI [(match_operand:V2DI 1 "register_operand" "v")
		      (match_operand:V2DI 2 "register_operand" "v")
		      (match_operand:V16QI 3 "register_operand" "v")
		      (match_operand:QI 4 "u3bit_cint_operand" "n")]
		     UNSPEC_XXPERMX))]
  "TARGET_POWER10"
  "xxpermx %x0,%x1,%x2,%x3,%4"
  [(set_attr "type" "vecperm")
   (set_attr "prefixed" "yes")])

;; XXEVAL built-in function support
(define_insn "xxeval"
  [(set (match_operand:V2DI 0 "register_operand" "=wa")
	(unspec:V2DI [(match_operand:V2DI 1 "register_operand" "wa")
		      (match_operand:V2DI 2 "register_operand" "wa")
		      (match_operand:V2DI 3 "register_operand" "wa")
		      (match_operand:QI 4 "u8bit_cint_operand" "n")]
		     UNSPEC_XXEVAL))]
   "TARGET_POWER10"
   "xxeval %x0,%x1,%x2,%x3,%4"
   [(set_attr "type" "vecperm")
    (set_attr "prefixed" "yes")])

;; Construct V1TI by vsx_concat_v2di
(define_split
  [(set (match_operand:V1TI 0 "vsx_register_operand")
	(subreg:V1TI
	  (match_operand:TI 1 "int_reg_operand") 0 ))]
  "TARGET_P9_VECTOR && !reload_completed"
  [(const_int 0)]
{
  rtx tmp1 = simplify_gen_subreg (DImode, operands[1], TImode, 0);
  rtx tmp2 = simplify_gen_subreg (DImode, operands[1], TImode, 8);
  rtx tmp3 = gen_reg_rtx (V2DImode);
  emit_insn (gen_vsx_concat_v2di (tmp3, tmp1, tmp2));
  rtx tmp4 = simplify_gen_subreg (V1TImode, tmp3, V2DImode, 0);
  emit_move_insn (operands[0], tmp4);
  DONE;
})

;; vmsumcud
(define_insn "vmsumcud"
[(set (match_operand:V1TI 0 "register_operand" "+v")
      (unspec:V1TI [(match_operand:V2DI 1 "register_operand" "v")
                    (match_operand:V2DI 2 "register_operand" "v")
		    (match_operand:V1TI 3 "register_operand" "v")]
		   UNSPEC_VMSUMCUD))]
  "TARGET_POWER10"
  "vmsumcud %0,%1,%2,%3"
  [(set_attr "type" "veccomplex")]
)
