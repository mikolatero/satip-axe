/*******************************************************************************
  This contains the functions to handle the platform driver.

  Copyright (C) 2007-2011  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/platform_device.h>
#include <linux/io.h>
#include "stmmac.h"

/**
 * stmmac_pltfr_probe
 * @pdev: platform device pointer
 * Description: platform_device probe function. It allocates
 * the necessary resources and invokes the main to init
 * the net device, register the mdio bus etc.
 */
static int stmmac_pltfr_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	void __iomem *addr = NULL;
	struct stmmac_priv *priv = NULL;
	struct plat_stmmacenet_data *plat_dat;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		pr_err("%s: ERROR: memory allocation failed"
		       "cannot get the I/O addr 0x%x\n",
		       __func__, (unsigned int)res->start);
		return -EBUSY;
	}

	addr = ioremap(res->start, resource_size(res));
	if (!addr) {
		pr_err("%s: ERROR: memory mapping failed", __func__);
		ret = -ENOMEM;
		goto out_release_region;
	}
	plat_dat = pdev->dev.platform_data;

	/* Custom initialisation (if needed)*/
	if (plat_dat->init) {
		ret = plat_dat->init(pdev);
		if (unlikely(ret))
			goto out_unmap;
	}

	priv = stmmac_dvr_probe(&(pdev->dev), plat_dat, addr);
	if (!priv) {
		pr_err("%s: main driver probe failed", __func__);
		goto out_unmap;
	}

	/* Get the MAC information */
	priv->dev->irq = platform_get_irq_byname(pdev, "macirq");
	if (priv->dev->irq == -ENXIO) {
		pr_err("%s: ERROR: MAC IRQ configuration "
		       "information not found\n", __func__);
		ret = -ENXIO;
		goto out_unmap;
	}

	/*
	 * On some platforms e.g. SPEAr the wake up irq differs from the mac irq
	 * The external wake up irq can be passed through the platform code
	 * named as "eth_wake_irq"
	 *
	 * In case the wake up interrupt is not passed from the platform
	 * so the driver will continue to use the mac irq (ndev->irq)
	 */
	priv->wol_irq = platform_get_irq_byname(pdev, "eth_wake_irq");
	if (priv->wol_irq == -ENXIO)
		priv->wol_irq = priv->dev->irq;

	priv->lpi_irq = platform_get_irq_byname(pdev, "eth_lpi");

	platform_set_drvdata(pdev, priv->dev);

	pm_runtime_set_active(&pdev->dev);
	pm_suspend_ignore_children(&pdev->dev, 1);
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	pr_debug("STMMAC platform driver registration completed");

	return 0;

out_unmap:
	iounmap(addr);
	platform_set_drvdata(pdev, NULL);

out_release_region:
	release_mem_region(res->start, resource_size(res));

	return ret;
}

/**
 * stmmac_pltfr_remove
 * @pdev: platform device pointer
 * Description: this function calls the main to free the net resources
 * and calls the platforms hook and release the resources (e.g. mem).
 */
static int stmmac_pltfr_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct resource *res;
	int ret = stmmac_dvr_remove(ndev);

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (priv->plat->exit)
		priv->plat->exit(pdev);

	if (priv->plat->exit)
		priv->plat->exit(pdev);

	platform_set_drvdata(pdev, NULL);

	iounmap((void *)priv->ioaddr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	return ret;
}

#ifdef CONFIG_PM
static int stmmac_pltfr_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_suspend(ndev);
}

static int stmmac_pltfr_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_resume(ndev);
}

int stmmac_pltfr_freeze(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_freeze(ndev);
}

int stmmac_pltfr_restore(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);

	return stmmac_restore(ndev);
}

#if 0
int stmmac_pltfr_idle(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct phy_device *phydev = priv->phydev;

#ifdef CONFIG_PM_RUNTIME
	if (!phydev->link)
		pm_schedule_suspend(dev, MSEC_PER_SEC * 5);
#endif
	return -EBUSY;
}
#endif

SIMPLE_DEV_PM_OPS(stmmac_pltfr_pm_ops, stmmac_pltfr_suspend, stmmac_pltfr_resume);
#else
static const struct dev_pm_ops stmmac_pltfr_pm_ops;
#endif /* CONFIG_PM */

static struct platform_driver stmmac_driver = {
	.probe = stmmac_pltfr_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		   .name = STMMAC_RESOURCE_NAME,
		   .owner = THIS_MODULE,
		   .pm = &stmmac_pltfr_pm_ops,
		   },
};

/**
 * stmmac_init_module - Entry point for the driver
 * Description: This function is the entry point for the driver.
 */
static int __init stmmac_init_module(void)
{
	return platform_driver_register(&stmmac_driver);
}

/**
 * stmmac_cleanup_module - Cleanup routine for the driver
 * Description: This function is the cleanup routine for the driver.
 */
static void __exit stmmac_cleanup_module(void)
{
	platform_driver_unregister(&stmmac_driver);
}

module_init(stmmac_init_module);
module_exit(stmmac_cleanup_module);

MODULE_DESCRIPTION("STMMAC 10/100/1000 Ethernet PLATFORM driver");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL");
