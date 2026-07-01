#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/property.h>

#define POLL_INTERVAL_MS 16

struct r43_key {
	u32 code;
	u32 byte_offset;
	u32 bit_mask;
};

struct r43_i2c_keys_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work work;
	u8 last_state[10];
	struct r43_key *keys;
	int nkeys;
};

/* Default hardcoded mappings if DTB nodes are absent */
static const struct r43_key default_keys[] = {
	{ KEY_UP, 0, 0x80 },
	{ KEY_DOWN, 0, 0x40 },
	{ KEY_LEFT, 0, 0x20 },
	{ KEY_RIGHT, 0, 0x10 },
	{ KEY_RIGHTSHIFT, 0, 0x08 },
	{ KEY_ENTER, 0, 0x04 },
	{ KEY_Y, 0, 0x02 },
	{ KEY_X, 0, 0x01 },
	{ KEY_END, 1, 0x80 }, /* Home Button */
};

static void r43_i2c_keys_work(struct work_struct *work)
{
	struct r43_i2c_keys_data *ddata = container_of(work, struct r43_i2c_keys_data, work.work);
	u8 buf[10];
	int ret, i;

	/* Read the full 10-byte state payload using the 0x01 command */
	ret = i2c_smbus_read_i2c_block_data(ddata->client, 0x01, sizeof(buf), buf);

	if (ret == sizeof(buf)) {
		/* Print state if the first 2 bytes (buttons) changed */
		if (memcmp(buf, ddata->last_state, 2) != 0) {
			dev_dbg(&ddata->client->dev, "I2C Buttons State: 0x%02x 0x%02x\n", buf[0], buf[1]);
			
			for (i = 0; i < ddata->nkeys; i++) {
				struct r43_key *key = &ddata->keys[i];
				if (key->byte_offset < sizeof(buf)) {
					/* Buttons are Active High (0x00 = resting, 0x80 = pressed) */
					input_report_key(ddata->input, key->code, !!(buf[key->byte_offset] & key->bit_mask));
				}
			}

			input_sync(ddata->input);
			memcpy(ddata->last_state, buf, 2);
		}
	}

	schedule_delayed_work(&ddata->work, msecs_to_jiffies(POLL_INTERVAL_MS));
}

static int r43_i2c_keys_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct r43_i2c_keys_data *ddata;
	struct input_dev *input;
	struct fwnode_handle *child;
	int ret, i = 0;
	int nkeys;

	ddata = devm_kzalloc(&client->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->client = client;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	input->name = "r43-i2c-keys";
	input->phys = "i2c-keys/input0";
	input->id.bustype = BUS_I2C;

	nkeys = device_get_child_node_count(&client->dev);
	if (nkeys == 0) {
		dev_info(&client->dev, "No child nodes found in DTB, using hardcoded defaults\n");
		ddata->nkeys = ARRAY_SIZE(default_keys);
		ddata->keys = devm_kmemdup(&client->dev, default_keys, sizeof(default_keys), GFP_KERNEL);
		if (!ddata->keys)
			return -ENOMEM;
	} else {
		dev_info(&client->dev, "Found %d child nodes in DTB\n", nkeys);
		ddata->nkeys = nkeys;
		ddata->keys = devm_kcalloc(&client->dev, nkeys, sizeof(*ddata->keys), GFP_KERNEL);
		if (!ddata->keys)
			return -ENOMEM;

		device_for_each_child_node(&client->dev, child) {
			u32 code, byte_offset, bit_mask;

			if (fwnode_property_read_u32(child, "linux,code", &code)) {
				dev_warn(&client->dev, "Missing linux,code for child\n");
				continue;
			}
			if (fwnode_property_read_u32(child, "byte-offset", &byte_offset))
				byte_offset = 0; /* Default to byte 0 */
			if (fwnode_property_read_u32(child, "bit-mask", &bit_mask)) {
				dev_warn(&client->dev, "Missing bit-mask for child\n");
				continue;
			}

			ddata->keys[i].code = code;
			ddata->keys[i].byte_offset = byte_offset;
			ddata->keys[i].bit_mask = bit_mask;
			i++;
		}
		ddata->nkeys = i; /* Actual valid keys parsed */
	}

	/* Enable capabilities for mapped keys */
	for (i = 0; i < ddata->nkeys; i++) {
		input_set_capability(input, EV_KEY, ddata->keys[i].code);
	}

	/* Enable Autorepeat (value 2 in evtest) */
	__set_bit(EV_REP, input->evbit);

	ddata->input = input;

	ret = input_register_device(input);
	if (ret) {
		dev_err(&client->dev, "Unable to register input device\n");
		return ret;
	}

	INIT_DELAYED_WORK(&ddata->work, r43_i2c_keys_work);
	schedule_delayed_work(&ddata->work, msecs_to_jiffies(POLL_INTERVAL_MS));

	i2c_set_clientdata(client, ddata);

	dev_info(&client->dev, "R43 I2C Keys probed successfully at 0x%02x\n", client->addr);
	return 0;
}

static int r43_i2c_keys_remove(struct i2c_client *client)
{
	struct r43_i2c_keys_data *ddata = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ddata->work);
	return 0;
}

static const struct i2c_device_id r43_i2c_keys_id[] = {
	{ "key_i2c3", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, r43_i2c_keys_id);

static const struct of_device_id r43_i2c_keys_of_match[] = {
	{ .compatible = "key_i2c3", },
	{ }
};
MODULE_DEVICE_TABLE(of, r43_i2c_keys_of_match);

static struct i2c_driver r43_i2c_keys_driver = {
	.driver = {
		.name	= "r43-i2c-keys",
		.of_match_table = r43_i2c_keys_of_match,
	},
	.probe		= r43_i2c_keys_probe,
	.remove		= r43_i2c_keys_remove,
	.id_table	= r43_i2c_keys_id,
};

module_i2c_driver(r43_i2c_keys_driver);

MODULE_AUTHOR("miguelargolo");
MODULE_DESCRIPTION("R43 I2C Keys Driver for key_i2c3");
MODULE_LICENSE("GPL v2");
