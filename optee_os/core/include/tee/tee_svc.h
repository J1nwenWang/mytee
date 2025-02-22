/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 */
#ifndef TEE_SVC_H
#define TEE_SVC_H

#include <assert.h>
#include <stdint.h>
#include <types_ext.h>
#include <tee_api_types.h>
#include <utee_types.h>

extern vaddr_t tee_svc_uref_base;

struct tee_ta_session;

/* TA Properties */
struct tee_props {
	const char *name;

	/* prop_type is of type enum user_ta_prop_type*/
	const uint32_t prop_type;

	/* either get_prop_func or both data and len */
	TEE_Result (*get_prop_func)(struct tee_ta_session *sess,
				    void *buf, size_t *blen);
	const void *data;
	const size_t len;
};

struct tee_vendor_props {
	const struct tee_props *props;
	size_t len;
};
void syscall_sys_return(unsigned long ret);

void syscall_log(const void *buf, size_t len);

void syscall_panic(unsigned long code);

TEE_Result syscall_not_supported(void);

/* prop_set defined by enum utee_property */
TEE_Result syscall_get_property(unsigned long prop_set,
				unsigned long index,
				void *name, uint32_t *name_len,
				void *buf, uint32_t *blen,
				uint32_t *prop_type);
TEE_Result syscall_get_property_name_to_index(unsigned long prop_set,
					      void *name,
					      unsigned long name_len,
					      uint32_t *index);

TEE_Result syscall_open_ta_session(const TEE_UUID *dest,
			unsigned long cancel_req_to, struct utee_params *params,
			uint32_t *sess, uint32_t *ret_orig);

TEE_Result syscall_close_ta_session(unsigned long sess);

TEE_Result syscall_invoke_ta_command(unsigned long sess,
			unsigned long cancel_req_to, unsigned long cmd_id,
			struct utee_params *params, uint32_t *ret_orig);

TEE_Result syscall_check_access_rights(unsigned long flags, const void *buf,
				       size_t len);

#ifdef CFG_WITH_USER_TA
TEE_Result tee_svc_copy_from_user(void *kaddr, const void *uaddr, size_t len);
#else
static inline TEE_Result tee_svc_copy_from_user(void *kaddr __unused,
						const void *uaddr __unused,
						size_t len __unused)
{
	return TEE_ERROR_NOT_SUPPORTED;
}
#endif

TEE_Result tee_svc_copy_to_user(void *uaddr, const void *kaddr, size_t len);

TEE_Result tee_svc_copy_kaddr_to_uref(uint32_t *uref, void *kaddr);

static inline uint32_t tee_svc_kaddr_to_uref(void *kaddr)
{
	assert(((vaddr_t)kaddr - tee_svc_uref_base) < UINT32_MAX);
	return (vaddr_t)kaddr - tee_svc_uref_base;
}

static inline vaddr_t tee_svc_uref_to_vaddr(uint32_t uref)
{
	return tee_svc_uref_base + uref;
}

static inline void *tee_svc_uref_to_kaddr(uint32_t uref)
{
	return (void *)tee_svc_uref_to_vaddr(uref);
}

TEE_Result syscall_get_cancellation_flag(uint32_t *cancel);

TEE_Result syscall_unmask_cancellation(uint32_t *old_mask);

TEE_Result syscall_mask_cancellation(uint32_t *old_mask);

TEE_Result syscall_wait(unsigned long timeout);

TEE_Result syscall_get_time(unsigned long cat, TEE_Time *time);
TEE_Result syscall_set_ta_time(const TEE_Time *time);
//TEE_Result syscall_mytee_fb(void);

#endif /* TEE_SVC_H */
