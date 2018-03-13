// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018, IBM Corporation
 */

#define pr_fmt(fmt) "of_nvdimm: " fmt

#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>

static const struct attribute_group *region_attr_groups[] = {
	&nd_region_attribute_group,
	&nd_device_attribute_group,
	NULL,
};

static int of_nvdimm_add_byte_addr_region(struct nvdimm_bus *bus,
					  struct device_node *np)
{
	struct nd_region_desc ndr_desc;
	struct resource temp_res;
	struct nd_region *region;

	/*
	 * byte regions should only have one address range
	 */
	if (of_address_to_resource(np, 0, &temp_res)) {
		pr_warn("Unable to parse reg[0] for %pOF\n", np);
		return -ENXIO;
	}

	pr_debug("Found %pR for %pOF\n", &temp_res, np);

	memset(&ndr_desc, 0, sizeof(ndr_desc));
	ndr_desc.res = &temp_res;
	ndr_desc.of_node = np;
	ndr_desc.attr_groups = region_attr_groups;
	ndr_desc.numa_node = of_node_to_nid(np);
	set_bit(ND_REGION_PAGEMAP, &ndr_desc.flags);

	if (of_device_is_compatible(np, "nvdimm-volatile"))
		region = nvdimm_volatile_region_create(bus, &ndr_desc);
	else
		region = nvdimm_pmem_region_create(bus, &ndr_desc);

	if (!region)
		return -ENXIO;

	return 0;
}

static const struct of_device_id of_nvdimm_dev_types[] = {
	{ .compatible = "nvdimm-persistent", },
	{ .compatible = "nvdimm-volatile", },
	{ },
};

/*
 * The nvdimm core refers to the bus descriptor structure at runtime
 * so we need to keep it around. Note that that this is different to
 * the other nd_*_desc types which are essentially containers for extra
 * function parameters.
 */
struct of_nd_private {
	struct nvdimm_bus_descriptor desc;
	struct nvdimm_bus *bus;
};

static const struct attribute_group *bus_attr_groups[] = {
	&nvdimm_bus_attribute_group,
	NULL,
};

static int of_nvdimm_probe(struct platform_device *pdev)
{
	struct device_node *node, *child;
	struct of_nd_private *priv;
	const struct of_device_id *match;

	node = dev_of_node(&pdev->dev);
	if (!node)
		return -ENXIO;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->desc.attr_groups = bus_attr_groups;
	priv->desc.provider_name = "of_nvdimm";
	priv->desc.module = THIS_MODULE;
	priv->desc.of_node = node;

	priv->bus = nvdimm_bus_register(&pdev->dev, &priv->desc);
	if (!priv->bus)
		goto err;

	platform_set_drvdata(pdev, priv);

	/* now walk the node bus and setup regions, etc */
	for_each_available_child_of_node(node, child) {
		match = of_match_node(of_nvdimm_dev_types, child);
		pr_debug("Parsed %pOF\n", child);
		if (!match)
			continue;
		of_platform_device_create(child, NULL, NULL);
		if (!of_nvdimm_add_byte_addr_region(priv->bus, child))
			continue;
	}

	return 0;

err:
	nvdimm_bus_unregister(priv->bus);
	kfree(priv);
	return -ENXIO;
}

static int of_nvdimm_remove(struct platform_device *pdev)
{
	struct of_nd_private *priv = platform_get_drvdata(pdev);

	if (!priv)
		return 0; /* possible? */

	device_for_each_child(&pdev->dev, NULL, of_platform_device_destroy);

	nvdimm_bus_unregister(priv->bus);
	kfree(priv);

	return 0;
}

static const struct of_device_id of_nvdimm_bus_match[] = {
	{ .compatible = "nvdimm-bus" },
	{ },
};

static struct platform_driver of_nvdimm_driver = {
	.probe = of_nvdimm_probe,
	.remove = of_nvdimm_remove,
	.driver = {
		.name = "of_nvdimm",
		.owner = THIS_MODULE,
		.of_match_table = of_nvdimm_bus_match,
	},
};

module_platform_driver(of_nvdimm_driver);
MODULE_DEVICE_TABLE(of, of_nvdimm_bus_match);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("IBM Corporation");
