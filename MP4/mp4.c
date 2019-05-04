#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>
#include "mp4_given.h"


#define MP4_SECURITY_NAME "security.mp4"
#define INODE_XATTR_LEN 255

static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp);

/**
 * get_inode_sid - Get the inode mp4 security label id
 *
 * @inode: the input inode
 *
 * @return the inode's security id if found.
 *
 */
static int get_inode_sid(struct inode *inode)
{
	pr_alert("Enter %s\n", __FUNCTION__);
	/*
	 * Add your code here
	 * ...
	 */

	char * buf;
	struct dentry *dentry;

	int rc;
	int xattr_cred;

	if(!inode)
		return -EINVAL;

	dentry = d_find_alias(inode);
	if(!dentry)
		return -EINVAL;

	buf = kmalloc(INODE_XATTR_LEN, GFP_KERNEL);

	if(!buf)
		return -ENOMEM;

	buf[INODE_XATTR_LEN] = '\0';

	if(inode->i_op->getxattr){
		rc = inode->i_op->getxattr(dentry, MP4_SECURITY_NAME,
					   buf, INODE_XATTR_LEN);
		if(rc){
			xattr_cred = __cred_ctx_to_sid(buf);
			return xattr_cred;
		}
		dput(dentry);
	} else {
		dput(dentry);
	}
	return 0;
}

/**
 * mp4_bprm_set_creds - Set the credentials for a new task
 *
 * @bprm: The linux binary preparation structure
 *
 * returns 0 on success.
 */
static int mp4_bprm_set_creds(struct linux_binprm *bprm)
{
	pr_alert("Enter %s\n", __FUNCTION__);

//	int mp4_sec_flag;
//	mp4_sec_flag = current_cred->mp4_flags;
	if(!bprm || !bprm->cred)
		return -EINVAL;

	struct mp4_security * security = bprm->cred->security;

	if(!security){
		mp4_cred_alloc_blank(bprm->cred, GFP_KERNEL);
		return 0;
	}

	int mp4_security_flags = get_inode_sid(bprm->file->f_inode);
	if(mp4_security_flags == MP4_TARGET_SID)
		security->mp4_flags = MP4_TARGET_SID;

	return 0;
}

/**
 * mp4_cred_alloc_blank - Allocate a blank mp4 security label
 *
 * @cred: the new credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
//	pr_alert("Enter %s\n", __FUNCTION__);

	struct mp4_security *security;

	if(!cred)
		return -EINVAL;

	security = (struct mp4_security *)kmalloc(sizeof(struct mp4_security), gfp);
	security->mp4_flags = MP4_NO_ACCESS;

	cred->security = security;
	if(!security)
		return -ENOMEM;



	return 0;
}


/**
 * mp4_cred_free - Free a created security label
 *
 * @cred: the credentials struct
 *
 */
static void mp4_cred_free(struct cred *cred)
{
//	pr_alert("Enter %s\n", __FUNCTION__);

	struct mp4_security *security = cred->security;

//	BUG_ON(cred->security && (unsigned long) cred->security < PAGE_SIZE);
	cred->security = (void *)NULL;
	kfree(security);
}

/**
 * mp4_cred_prepare - Prepare new credentials for modification
 *
 * @new: the new credentials
 * @old: the old credentials
 * @gfp: the atomicity of the memory allocation
 *
 */
static int mp4_cred_prepare(struct cred *new, const struct cred *old,
			    gfp_t gfp)
{
//	pr_alert("Enter %s\n", __FUNCTION__);

	const struct mp4_security *old_mp4sec;
	struct mp4_security *mp4_sec;

	if(!old || !old->security){
		mp4_cred_alloc_blank(new, gfp);
		return 0;
	}

	old_mp4sec = (const struct mp4_security *)old->security;

	if(!old_mp4sec){
		mp4_sec = kmemdup(old_mp4sec, sizeof(struct mp4_security), gfp);
		if(!mp4_sec){
//			pr_alert("Returning -ENOMEM at %d\n", __LINE__);
			return -ENOMEM;
		}

//		pr_alert("Reached %d\n", __LINE__);
		new->security = mp4_sec;
	}

//	pr_alert("Returning at %d\n", __LINE__);
	return 0;
}

/**
 * mp4_inode_init_security - Set the security attribute of a newly created inode
 *
 * @inode: the newly created inode
 * @dir: the containing directory
 * @qstr: unused
 * @name: where to put the attribute name
 * @value: where to put the attribute value
 * @len: where to put the length of the attribute
 *
 * returns 0 if all goes well, -ENOMEM if no memory, -EOPNOTSUPP to skip
 *
 */
static int mp4_inode_init_security(struct inode *inode, struct inode *dir,
				   const struct qstr *qstr,
				   const char **name, void **value, size_t *len)
{
//	pr_alert("Enter %s\n", __FUNCTION__);
	/*
	 * Add your code here
	 * ...
	 */
	return 0;
}

/**
 * mp4_has_permission - Check if subject has permission to an object
 *
 * @ssid: the subject's security id
 * @osid: the object's security id
 * @mask: the operation mask
 *
 * returns 0 is access granter, -EACCES otherwise
 *
 */
static int mp4_has_permission(int ssid, int osid, int mask)
{
//	pr_alert("Enter %s\n", __FUNCTION__);
	/*
	 * Add your code here
	 * ...
	 */
	return 0;
}

/**
 * mp4_inode_permission - Check permission for an inode being opened
 *
 * @inode: the inode in question
 * @mask: the access requested
 *
 * This is the important access check hook
 *
 * returns 0 if access is granted, -EACCES otherwise
 *
 */
static int mp4_inode_permission(struct inode *inode, int mask)
{
//	pr_alert("Enter %s\n", __FUNCTION__);
	/*
	 * Add your code here
	 * ...
	 */
	return 0;
}


/*
 * This is the list of hooks that we will using for our security module.
 */
static struct security_hook_list mp4_hooks[] = {
	/*
	 * inode function to assign a label and to check permission
	 */
	LSM_HOOK_INIT(inode_init_security, mp4_inode_init_security),
	LSM_HOOK_INIT(inode_permission, mp4_inode_permission),

	/*
	 * setting the credentials subjective security label when laucnhing a
	 * binary
	 */
	LSM_HOOK_INIT(bprm_set_creds, mp4_bprm_set_creds),

	/* credentials handling and preparation */
	LSM_HOOK_INIT(cred_alloc_blank, mp4_cred_alloc_blank),
	LSM_HOOK_INIT(cred_free, mp4_cred_free),
	LSM_HOOK_INIT(cred_prepare, mp4_cred_prepare)
};

static __init int mp4_init(void)
{
	/*
	 * check if mp4 lsm is enabled with boot parameters
	 */
	if (!security_module_enable("mp4"))
		return 0;

	pr_alert("mp4 LSM initializing..");

	/*
	 * Register the mp4 hooks with lsm
	 */
	security_add_hooks(mp4_hooks, ARRAY_SIZE(mp4_hooks));

	return 0;
}

/*
 * early registration with the kernel
 */
security_initcall(mp4_init);
