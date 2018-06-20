#include "relocator-arm64.h"
#include "ARM64AssemblyCore.h"
#include "std_kit/std_kit.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

ARM64Relocator *arm64_assembly_relocator_cclass(new)(ARM64AssemblyReader *input, ARM64AssemblyWriter *output) {
    ARM64Relocator *relocator = SAFE_MALLOC_TYPE(ARM64Relocator);
    relocator->input          = input;
    relocator->output         = output;
    return relocator;
}

void arm64_assembly_relocator_cclass(reset)(ARM64Relocator *self) {
    arm64_assembly_reader_reset(self->input, 0, 0);
    arm64_assembly_writer_reset(self->output, 0);
    list_destroy(self->literal_instCTXs);
    list_destroy(self->io_indexs);
}

void arm64_assembly_relocator_cclass(try_relocate)(void *address, int bytes_min, int *bytes_max) {
    int tmp_size   = 0;
    bool early_end = false;

    ARM64InstructionCTX *instCTX;
    ARM64AssemblyReader *reader = arm64_assembly_reader_cclass(new)(address, address);

    do {
        instCTX = arm64_assembly_reader_cclass(read_inst)(reader);
        switch (getInstType(instCTX->bytes)) {
        case BImm:
            early_end = true;
            break;
        default:;
        }
        tmp_size += instCTX->size;

    } while (tmp_size < bytes_min);

    if (early_end) {
        *bytes_max = bytes_min;
    }
    // TODO: free ARM64AssemblyReader
    SAFE_FREE(reader);
}

void arm64_assembly_relocator_cclass(relocate_to)(ARM64Relocator *self, void *target_address) {
    list_iterator_t *it = list_iterator_new(self->literal_instCTXs, LIST_HEAD);
    for (int i; i < self->literal_instCTXs->len; i++) {
        ARM64InstructionCTX *instCTX = (ARM64InstructionCTX *)(list_at(self->literal_instCTXs, i)->val);
        zz_addr_t literal_target_address;
        literal_target_address = *(zz_addr_t *)instCTX->address;
        if (literal_target_address > (zz_addr_t)self->input->start_pc &&
            literal_target_address < ((zz_addr_t)self->input->start_pc + self->input->inst_bytes->size)) {
            list_iterator_t *it_a = list_iterator_new(self->io_indexs, LIST_HEAD);
            for (int j; j < self->io_indexs->len; j++) {
                io_index_t *io_index = (io_index_t *)(list_at(self->io_indexs, j)->val);
                int i_index          = io_index->input_index;
                int o_index          = io_index->output_index;
                ARM64InstructionCTX *inputInstCTX =
                    (ARM64InstructionCTX *)(list_at(self->input->instCTXs, i_index)->val);
                ARM64InstructionCTX *outputInstCTX =
                    (ARM64InstructionCTX *)(list_at(self->output->instCTXs, o_index)->val);
                if (inputInstCTX->address == literal_target_address) {
                    *(zz_addr_t *)instCTX->address =
                        ((ARM64InstructionCTX *)(list_at(self->output->instCTXs, o_index)->val))->pc -
                        (zz_addr_t)self->output->start_pc + (zz_addr_t)target_address;
                    break;
                }
            }
        }
    }
}

void arm64_assembly_relocator_cclass(double_write)(ARM64Relocator *self, void *target_address) {
    assert((zz_addr_t)target_address % 4 == 0);

    int origin_inst_buffer_size = self->output->inst_bytes->size;
    arm64_assembly_writer_cclass(reset)(self->output, 0);

    list_destroy(self->literal_instCTXs);
    list_destroy(self->io_indexs);

    arm64_assembly_relocator_cclass(relocate_write_all)(self);
    void *no_need_relocate_inst_buffer =
        (void *)((zz_addr_t)self->output->inst_bytes->data + self->output->inst_bytes->size);

    arm64_assembly_writer_cclass(put_bytes)(self->output, no_need_relocate_inst_buffer,
                                            origin_inst_buffer_size - self->output->inst_bytes->size);
}

void arm64_assembly_relocator_cclass(register_literal_instCTX)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    list_rpush(self->literal_instCTXs, list_node_new(instCTX));
}

void arm64_assembly_relocator_cclass(relocate_write_all)(ARM64Relocator *self) {
    do {
        arm64_assembly_relocator_cclass(relocate_write)(self);
    } while (0);
}

void arm64_assembly_relocator_cclass(relocate_write)(ARM64Relocator *self) {
    ARM64InstructionCTX *instCTX;
    bool rewritten = true;

    int done_relocated_input_count;
    done_relocated_input_count = self->io_indexs->len;

    if (self->input->instCTXs->len < self->io_indexs->len) {
        instCTX = (ARM64InstructionCTX *)(list_at(self->input->instCTXs, done_relocated_input_count)->val);
    } else
        return;

    switch (getInstType(instCTX->bytes)) {
    case LoadLiteral:
        arm64_assembly_relocator_cclass(rewrite_LoadLiteral)(self, instCTX);
        break;
    case BaseCmpBranch:
        arm64_assembly_relocator_cclass(rewrite_BaseCmpBranch)(self, instCTX);
        break;
    case BranchCond:
        arm64_assembly_relocator_cclass(rewrite_BranchCond)(self, instCTX);
        break;
    case B:
        arm64_assembly_relocator_cclass(rewrite_B)(self, instCTX);
        break;
    case BL:
        arm64_assembly_relocator_cclass(rewrite_BL)(self, instCTX);
        break;
    default:
        rewritten = false;
        break;
    }
    if (!rewritten) {
        arm64_assembly_writer_cclass(put_bytes)(self->output, (void *)&instCTX->bytes, instCTX->size);
    }

    // push relocate input <-> output index
    io_index_t *io_index   = SAFE_MALLOC_TYPE(io_index_t);
    io_index->input_index  = done_relocated_input_count;
    io_index->output_index = self->output->instCTXs->len;
    list_rpush(self->io_indexs, list_node_new(io_index));
}

void arm64_assembly_relocator_cclass(rewrite_LoadLiteral)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    uint32_t Rt, label;
    int index;
    zz_addr_t target_address;
    Rt             = get_insn_sub(instCTX->bytes, 0, 5);
    label          = get_insn_sub(instCTX->bytes, 5, 19);
    target_address = (label << 2) + instCTX->pc;

    /*
        0x1000: ldr Rt, #0x8
        0x1004: b #0xc
        0x1008: .long 0x4321
        0x100c: .long 0x8765
        0x1010: ldr Rt, Rt
    */
    ARM64Reg regRt = arm64_register_disdescribe(Rt, 0);
    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, regRt, 0x8);
    arm64_assembly_writer_cclass(put_b_imm)(self->output, 0xc);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);
    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&target_address, sizeof(target_address));
    arm64_assembly_writer_cclass(put_ldr_reg_reg_offset)(self->output, regRt, regRt, 0);
}

void arm64_assembly_relocator_cclass(rewrite_BaseCmpBranch)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    uint32_t target;
    uint32_t inst32;
    zz_addr_t target_address;

    inst32 = instCTX->bytes;

    target         = get_insn_sub(inst32, 5, 19);
    target_address = (target << 2) + instCTX->pc;

    target = 0x8 >> 2;
    BIT32SET(&inst32, 5, 19, target);
    arm64_assembly_writer_cclass(put_bytes)(self->output, &inst32, instCTX->size);

    arm64_assembly_writer_cclass(put_b_imm)(self->output, 0x14);
    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, ARM64_REG_X17, 0x8);
    arm64_assembly_writer_cclass(put_br_reg)(self->output, ARM64_REG_X17);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);
    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&target_address, sizeof(zz_ptr_t));
}

void arm64_assembly_relocator_cclass(rewrite_BranchCond)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    uint32_t target;
    uint32_t inst32;
    zz_addr_t target_address;

    inst32 = instCTX->bytes;

    target         = get_insn_sub(inst32, 5, 19);
    target_address = (target << 2) + instCTX->pc;

    target = 0x8 >> 2;
    BIT32SET(&inst32, 5, 19, target);
    arm64_assembly_writer_cclass(put_bytes)(self->output, &inst32, instCTX->size);

    arm64_assembly_writer_cclass(put_b_imm)(self->output, 0x14);
    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, ARM64_REG_X17, 0x8);
    arm64_assembly_writer_cclass(put_br_reg)(self->output, ARM64_REG_X17);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);
    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&target_address, sizeof(zz_ptr_t));
}

void arm64_assembly_relocator_cclass(rewrite_B)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    uint32_t addr;
    zz_addr_t target_address;

    addr = get_insn_sub(instCTX->bytes, 0, 26);

    target_address = (addr << 2) + instCTX->pc;

    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, ARM64_REG_X17, 0x8);
    arm64_assembly_writer_cclass(put_br_reg)(self->output, ARM64_REG_X17);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);

    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&target_address, sizeof(zz_ptr_t));
}

void arm64_assembly_relocator_cclass(rewrite_BL)(ARM64Relocator *self, ARM64InstructionCTX *instCTX) {
    uint32_t op, addr;
    zz_addr_t target_address, next_pc_address;

    addr = get_insn_sub(instCTX->bytes, 0, 26);

    target_address  = (addr << 2) + instCTX->pc;
    next_pc_address = instCTX->pc + 4;

    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, ARM64_REG_X17, 0xc);
    arm64_assembly_writer_cclass(put_blr_reg)(self->output, ARM64_REG_X17);
    arm64_assembly_writer_cclass(put_b_imm)(self->output, 0xc);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);

    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&target_address, sizeof(zz_ptr_t));

    arm64_assembly_writer_cclass(put_ldr_reg_imm)(self->output, ARM64_REG_X17, 0x8);
    arm64_assembly_writer_cclass(put_br_reg)(self->output, ARM64_REG_X17);
    arm64_assembly_relocator_cclass(register_literal_instCTX)(
        self, (ARM64InstructionCTX *)(list_at(self->output->instCTXs, self->output->instCTXs->len))->val);
    arm64_assembly_writer_cclass(put_bytes)(self->output, (zz_ptr_t)&next_pc_address, sizeof(zz_ptr_t));
}