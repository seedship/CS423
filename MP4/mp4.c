#define pr_fmt(fmt) "cs423_mp4: " fmt

#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/binfmts.h>
#include "mp4_given.h"

#define INODE_XATTR_LEN 255

//forward declaring
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
	//File system variables
	char *buf;
	struct dentry *dentry;

	//Credential fetching variables
	int rc;
	int xattr_cred;

	//Sanitizing variable
	if(!inode){
		pr_alert("Returning EINVAL at %d\n", __LINE__);
		return -EINVAL;
	}

	dentry = d_find_alias(inode);
	if(!dentry){
		pr_alert("Returning EINVAL at %d\n", __LINE__);
		return -EINVAL;
	}

	//Allocate a buffer to copy XATTR data too
	buf = kmalloc(INODE_XATTR_LEN, GFP_KERNEL);
	if(!buf){
		pr_alert("Returning ENOMEM at %d\n", __LINE__);
		return -ENOMEM;
	}

	buf[INODE_XATTR_LEN] = '\0';

	//Get XATTR data
	if(inode->i_op->getxattr){
		rc = inode->i_op->getxattr(dentry, XATTR_NAME_MP4,
								   buf, INODE_XATTR_LEN);
		dput(dentry);
		if(rc && rc != -ENODATA){
			buf[rc] = '\0';
			xattr_cred = __cred_ctx_to_sid(buf);
			//Return XATTR if exists
			kfree(buf);
			return xattr_cred;
		}
	}
	//Else return MP4_NO_ACCESS
	kfree(buf);
	return MP4_NO_ACCESS;
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
	//Security variables
	struct mp4_security * security;
	int mp4_security_flags;

	//Sanitizing input
	if(!bprm || !bprm->cred){
		pr_alert("Returning EINVAL at %d\n", __LINE__);
		return -EINVAL;
	}

	security = bprm->cred->security;

	//If inode has no credentials, give task blank creds
	if(!security){
		mp4_cred_alloc_blank(bprm->cred, GFP_KERNEL);
		return 0;
	}

	//Fetch inode flags and set task to target if necessary
	mp4_security_flags = get_inode_sid(bprm->file->f_inode);
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
	//Security bookkeeping variables
	struct mp4_security *security;

	//Sanitizing input
	if(!cred){
		pr_alert("Returning EINVAL at %d\n", __LINE__);
		return -EINVAL;
	}

	//Allocate memory
	security = (struct mp4_security *)kmalloc(sizeof(struct mp4_security), gfp);
	if(!security){
		pr_alert("Returning ENOMEM at %d\n", __LINE__);
		return -ENOMEM;
	}

	//Set flag to default value
	security->mp4_flags = MP4_NO_ACCESS;

	//Set security
	cred->security = security;

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
	//Security helper pointer
	struct mp4_security *security;

	//Santizing input
	if(!cred || !cred->security){
		pr_alert("Null pointers in cred_free! %d\n", __LINE__);
		return;
	}

	//Copy the pointer
	security = cred->security;

	//Set security to NULL before free, in case anyone accesses it
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
	//Security struct for new cred
	struct mp4_security *mp4_sec;

	//Sanitizing input. If old creds not valid, give new cred blank creds
	if(!old || !old->security){
		mp4_cred_alloc_blank(new, gfp);
		return 0;
	}

	//Allocate
	mp4_sec = kmemdup(old->security, sizeof(struct mp4_security), gfp);
	if(!mp4_sec){
		pr_alert("Returning -ENOMEM at %d\n", __LINE__);
		return -ENOMEM;
	}

	//Update new cred's security
	new->security = mp4_sec;

	return 0;
}

/**
 * mp4_inode_init_security - Set the security attribute of a newly created inode
 *
 * @inode: the newly created i
			if(printk_ratelimit()){
				pr_alert("rc is %d, buf is %s, returning %d\n", rc, buf, xattr_cred);
			}node
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
	//Helper pointer
	const struct mp4_security tsec;

	*tsec = current_security();

	//Input sanatizing
	if(!tsec){
		return -EOPNOTSUPP;
	}

	//If target contains the target SID, set the name, value, and len
	if(tsec->mp4_flags == MP4_TARGET_SID){
		if(name && value && len){
			if(S_ISDIR(inode->i_mode)) {
				*name = kstrdup(XATTR_MP4_SUFFIX, GFP_KERNEL);
				*value = kstrdup("dir-write", GFP_KERNEL);
				*len = strlen(*value);
			} else {
				*name = kstrdup(XATTR_MP4_SUFFIX, GFP_KERNEL);
				*value = kstrdup("read-write", GFP_KERNEL);
				*len = strlen(*value);
			}
			return 0;
		} else {
			pr_alert("%d: Invalid pointers: name: %p value: %p len: %p\n", __LINE__, name, value, len);
			return -EOPNOTSUPP;
		}
	}

	return -EOPNOTSUPP;
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
	//Helper variables
	const struct mp4_security *tsec;
	struct dentry *dentry;
	char * buf;
	char* path;
	int inode_sec;

	//Input sanitization
	if(!inode){
		pr_alert("%d inode NULL!\n", __LINE__);
		return 0;
	}

	//Get dentry
	dentry = d_find_alias(inode);
	if(!dentry){
		dput(dentry);
		pr_alert("%d dentry NULL!\n", __LINE__);
		return 0;
	}

	//Allocate a buffer
	buf = kmalloc(255 * sizeof(char), GFP_KERNEL);
	if(!buf){
		pr_alert("mp4.c:%d No memory\n", __LINE__);
		return 0;
	}
	buf[254] = '\0';

	//Get path of inode
	path = dentry_path_raw(dentry, buf, 254);
	dput(dentry);

	if(mp4_should_skip_path(path)){
		return 0;
	}

	tsec = current_security();
	inode_sec = get_inode_sid(inode);

	//Snaitization check
	if(inode_sec < 0 || inode_sec > 6){
		pr_alert("inode sec is: %d, which is out of bounds\n", inode_sec);
	}

	//Implementation of flowchart in Figure 2
	if(tsec && tsec->mp4_flags == MP4_TARGET_SID){
		switch (inode_sec) {
			case MP4_NO_ACCESS:
				pr_info("Unallowed access: inode: %s has MP4_NO_ACCESS but requested operation has 0x%x\n", path, mask);
				goto BAD;
			case MP4_READ_OBJ:
				if(mask & (MAY_APPEND | MAY_WRITE | MAY_EXEC)){
					pr_info("Unallowed access: inode: %s has MP4_READ_OBJ but requested operation has 0x%x\n", path, mask);
					goto BAD;
				}
				break;
			case MP4_READ_WRITE:
				if(mask & (MAY_EXEC)){
					pr_info("Unallowed access: inode: %s has MP4_READ_WRITE but requested operation has MAY_EXEC\n", path);
					goto BAD;
				}
				break;
			case MP4_WRITE_OBJ:
				if(mask & (MAY_READ | MAY_EXEC)){
					pr_info("Unallowed access: inode: %s has MP4_WRITE_OBJ but requested operation has 0x%x\n", path, mask);
					goto BAD;
				}
				break;
			case MP4_EXEC_OBJ:
				if(mask & (MAY_APPEND | MAY_WRITE)){
					pr_info("Unallowed access: inode: %s has MP4_EXEC_OBJ but requested operation has 0x%x\n", path, mask);
					goto BAD;
				}
				break;
			case MP4_READ_DIR:
				if(mask & (MAY_APPEND | MAY_WRITE)){
					pr_info("Unallowed access: inode: %s has MP4_READ_DIR but requested operation has 0x%x\n", path, mask);
					goto BAD;
				}
			default:
				break;
		}
	} else {
		if(S_ISDIR(inode->i_mode)) {
			//ALLOW ACCESS
		} else {
			switch (inode_sec) {
				case MP4_READ_OBJ:
					if(mask & (MAY_APPEND | MAY_WRITE | MAY_EXEC)){
						pr_info("Unallowed access: inode: %s has MP4_READ_OBJ but requested operation has 0x%x\n", path, mask);
						goto BAD;
					}
					break;
				case MP4_READ_WRITE:
					if(mask & (MAY_APPEND | MAY_WRITE | MAY_EXEC)){
						pr_info("Unallowed access: inode: %s has MP4_READ_WRITE but requested operation has 0x%x\n", path, mask);
						goto BAD;
					}
					break;
				case MP4_WRITE_OBJ:
					if(mask & (MAY_APPEND | MAY_WRITE | MAY_EXEC)){
						pr_info("Unallowed access: inode: %s has MP4_WRITE_OBJ but requested operation has 0x%x\n", path, mask);
						goto BAD;
					}
					break;
				case MP4_EXEC_OBJ:
					if(mask & (MAY_APPEND | MAY_WRITE)){
						pr_info("Unallowed access: inode: %s has MP4_EXEC_OBJ but requested operation has 0x%x\n", path, mask);
						goto BAD;
					}
					break;
				default:
					//ALLOW ACCESS
					break;
			}
		}
	}


	/*
	 * Add your code here
	 * ...
	 */

	kfree(buf);
	return 0;
BAD:
	kfree(buf);
	return -EACCES;
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

	pr_alert("mp4 LSM initializing...\n");

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
