#pragma once

struct nfs_vnode;
struct nfs_fs;
struct nfs_cb;
struct nfs_context;

void nfs_mount_cb(int status, struct nfs_context *nfs, void *data, void *private_data);
struct vnode *nfs_bootstrap(struct nfs_context *context);