// SPDX-License-Identifier: GPL-2.0
/*
 * Virtio transport on top of the host interface (asm/host.h).
 *
 * The host lists its devices (virtio_probe); each one becomes a
 * virtio_device. The rings are ordinary split virtqueues in kernel memory,
 * which the host reads and writes directly: the kernel only tells it where
 * they are (virtio_queue), kicks it (virtio_notify) and is told about used
 * buffers through interrupt WASM_IRQ_VIRTIO0 + device index.
 *
 * The idea of a host-call transport is from arch/wasm/drivers/virtio_wasm.c
 * in tombl/linux; this one is written for one CPU and a synchronous host.
 */
#define pr_fmt(fmt) "virtio-wasm: " fmt

#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <asm/host.h>
#include <asm/irq.h>

#define WASM_VIRTIO_QUEUE_SIZE	256

struct wasm_virtio_device {
	struct virtio_device vdev;
	u32 index;
	u8 status;
	u64 features;
	u32 config_len;
	char name[16];
};

#define to_wasm_virtio(_vdev) \
	container_of(_vdev, struct wasm_virtio_device, vdev)

static void wv_get(struct virtio_device *vdev, unsigned int offset, void *buf,
		   unsigned int len)
{
	struct wasm_virtio_device *wv = to_wasm_virtio(vdev);

	memset(buf, 0, len);
	if (offset >= wv->config_len)
		return;
	wasm_host_virtio_config_read(wv->index, offset, buf,
				     min(len, wv->config_len - offset));
}

static void wv_set(struct virtio_device *vdev, unsigned int offset,
		   const void *buf, unsigned int len)
{
	/* FIXME(wasm): the host interface has no config space writes. */
	dev_warn(&vdev->dev, "config write of %u bytes at %u ignored\n",
		 len, offset);
}

static u8 wv_get_status(struct virtio_device *vdev)
{
	return to_wasm_virtio(vdev)->status;
}

static void wv_set_status(struct virtio_device *vdev, u8 status)
{
	struct wasm_virtio_device *wv = to_wasm_virtio(vdev);

	wv->status = status;
	wasm_host_virtio_set_status(wv->index, status);
}

/* The host is done with all buffers when virtio_set_status(0) returns. */
static void wv_reset(struct virtio_device *vdev)
{
	wv_set_status(vdev, 0);
}

static bool wv_notify(struct virtqueue *vq)
{
	wasm_host_virtio_notify(to_wasm_virtio(vq->vdev)->index, vq->index);
	return true;
}

static irqreturn_t wv_interrupt(int irq, void *data)
{
	struct wasm_virtio_device *wv = data;
	struct virtqueue *vq;
	irqreturn_t ret = IRQ_NONE;

	virtio_device_for_each_vq(&wv->vdev, vq)
		ret |= vring_interrupt(irq, vq);
	return ret;
}

static void wv_del_vqs(struct virtio_device *vdev)
{
	struct wasm_virtio_device *wv = to_wasm_virtio(vdev);
	struct virtqueue *vq, *n;

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		wasm_host_virtio_queue(wv->index, vq->index, 0, 0, 0, 0);
		vring_del_virtqueue(vq);
	}
	free_irq(WASM_IRQ_VIRTIO0 + wv->index, wv);
}

static int wv_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
		       struct virtqueue *vqs[],
		       struct virtqueue_info vqs_info[],
		       struct irq_affinity *desc)
{
	struct wasm_virtio_device *wv = to_wasm_virtio(vdev);
	unsigned int i, index = 0;
	int ret;

	ret = request_irq(WASM_IRQ_VIRTIO0 + wv->index, wv_interrupt, 0,
			  wv->name, wv);
	if (ret)
		return ret;

	for (i = 0; i < nvqs; i++) {
		struct virtqueue_info *vqi = &vqs_info[i];
		struct virtqueue *vq;

		if (!vqi->name) {
			vqs[i] = NULL;
			continue;
		}
		vq = vring_create_virtqueue(index, WASM_VIRTIO_QUEUE_SIZE,
					    PAGE_SIZE, vdev, true, true,
					    vqi->ctx, wv_notify, vqi->callback,
					    vqi->name);
		if (!vq) {
			wv_del_vqs(vdev);
			return -ENOMEM;
		}
		vq->num_max = virtqueue_get_vring_size(vq);
		wasm_host_virtio_queue(wv->index, index,
				       virtqueue_get_vring_size(vq),
				       virtqueue_get_desc_addr(vq),
				       virtqueue_get_avail_addr(vq),
				       virtqueue_get_used_addr(vq));
		vqs[i] = vq;
		index++;
	}
	return 0;
}

static u64 wv_get_features(struct virtio_device *vdev)
{
	return to_wasm_virtio(vdev)->features;
}

static int wv_finalize_features(struct virtio_device *vdev)
{
	struct wasm_virtio_device *wv = to_wasm_virtio(vdev);

	vring_transport_features(vdev);
	if (!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "the host device is not virtio 1.0\n");
		return -EINVAL;
	}
	wasm_host_virtio_set_features(wv->index, (u32)vdev->features,
				      (u32)(vdev->features >> 32));
	return 0;
}

static const char *wv_bus_name(struct virtio_device *vdev)
{
	return to_wasm_virtio(vdev)->name;
}

static const struct virtio_config_ops wasm_virtio_config_ops = {
	.get			= wv_get,
	.set			= wv_set,
	.get_status		= wv_get_status,
	.set_status		= wv_set_status,
	.reset			= wv_reset,
	.find_vqs		= wv_find_vqs,
	.del_vqs		= wv_del_vqs,
	.get_features		= wv_get_features,
	.finalize_features	= wv_finalize_features,
	.bus_name		= wv_bus_name,
};

static void wv_release_dev(struct device *dev)
{
	kfree(to_wasm_virtio(dev_to_virtio(dev)));
}

static int __init wasm_virtio_init(void)
{
	struct wasm_virtio_info info;
	struct device *parent;
	u32 index;

	if (wasm_host_virtio_probe(0, &info))
		return 0;

	parent = root_device_register("wasm-host");
	if (IS_ERR(parent))
		return PTR_ERR(parent);

	for (index = 0; index < WASM_VIRTIO_MAX; index++) {
		struct wasm_virtio_device *wv;
		int ret;

		if (wasm_host_virtio_probe(index, &info))
			break;
		wv = kzalloc(sizeof(*wv), GFP_KERNEL);
		if (!wv)
			return -ENOMEM;
		wv->index = index;
		wv->features = (u64)info.features_hi << 32 | info.features_lo;
		wv->config_len = info.config_len;
		snprintf(wv->name, sizeof(wv->name), "wasm-virtio%u", index);
		wv->vdev.id.device = info.device_id;
		wv->vdev.id.vendor = info.vendor_id;
		wv->vdev.config = &wasm_virtio_config_ops;
		wv->vdev.dev.parent = parent;
		wv->vdev.dev.release = wv_release_dev;

		ret = register_virtio_device(&wv->vdev);
		if (ret) {
			pr_err("device %u (id %u): %d\n", index,
			       info.device_id, ret);
			put_device(&wv->vdev.dev);
		}
	}
	pr_info("%u host device%s\n", index, index == 1 ? "" : "s");
	return 0;
}
device_initcall(wasm_virtio_init);
