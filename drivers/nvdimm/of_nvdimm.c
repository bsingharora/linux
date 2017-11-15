/*
 * Copyright 2017, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
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

static int of_nvdimm_add_byte(struct nvdimm_bus *bus, struct device_node *np)
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
#ifdef CONFIG_NUMA
	ndr_desc.numa_node = of_node_to_nid(np);
#endif
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
	{ .compatible = "nvdimm-persistent", .data = of_nvdimm_add_byte },
	{ .compatible = "nvdimm-volatile", .data = of_nvdimm_add_byte },
	{ },
};

static void of_nvdimm_parse_one(struct nvdimm_bus *bus,
		struct device_node *node)
{
	int (*parse_node)(struct nvdimm_bus *, struct device_node *);
	const struct of_device_id *match;
	int rc;

	if (of_node_test_and_set_flag(node, OF_POPULATED)) {
		pr_debug("%pOF already parsed, skipping\n", node);
		return;
	}

	match = of_match_node(of_nvdimm_dev_types, node);
	if (!match) {
		pr_info("No compatible match for '%pOF'\n", node);
		of_node_clear_flag(node, OF_POPULATED);
		return;
	}

	of_node_get(node);
	parse_node = match->data;
	rc = parse_node(bus, node);

	if (rc) {
		of_node_clear_flag(node, OF_POPULATED);
		of_node_put(node);
	}

	pr_debug("Parsed %pOF, rc = %d\n", node, rc);

	return;
}

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
	for_each_available_child_of_node(node, child)
		of_nvdimm_parse_one(priv->bus, child);

	return 0;

err:
	nvdimm_bus_unregister(priv->bus);
	kfree(priv);
	return -ENXIO;
}

static int of_nvdimm_remove(struct platform_device *pdev)
{
	struct of_nd_private *priv = platform_get_drvdata(pdev);
	struct device_node *node;

	if (!priv)
		return 0; /* possible? */

	for_each_available_child_of_node(pdev->dev.of_node, node) {
		if (!of_node_check_flag(node, OF_POPULATED))
			continue;

		of_node_clear_flag(node, OF_POPULATED);
		of_node_put(node);
		pr_debug("de-populating %s\n", node->full_name);
	}

	nvdimm_bus_unregister(priv->bus);
	kfree(priv);

	return 0;
}

static const struct of_device_id of_nvdimm_bus_match[] = {
	{ .compatible = "nvdimm-bus" },
	{ },
};

static const struct platform_driver of_nvdimm_driver = {
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
