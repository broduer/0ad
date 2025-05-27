export const ObservableMixin = (Parent) => class Observable extends (Parent ?? Object)
{
	constructor(...args)
	{
		super(...args);

		Object.defineProperty(this, "_changeObserver", {
			"value": {},
			"enumerable": false,
		});
		Object.defineProperty(this, "_setObserver", {
			"value": {},
			"enumerable": false,
		});

		return new Proxy(this, {
			"set": (target, key, value) =>
			{
				let old;
				let hasOld = false;
				if (Reflect.has(target, key))
				{
					hasOld = true;
					old = Reflect.get(target, key);
				}
				Reflect.set(target, key, value);
				this._trigger(this._setObserver, key, old);
				if (!hasOld || old !== value)
					this._trigger(this._changeObserver, key, old);
				return true;
			}
		});
	}

	_trigger(dict, key, old)
	{
		if (dict[key])
			for (const watcher of dict[key])
				watcher(key, old);
	}

	trigger(key, old)
	{
		this._trigger(this._setObserver, key, old);
		this._trigger(this._changeObserver, key, old);
	}

	watch(watcher, props, onlyChange = true)
	{
		const dic = onlyChange ? this._changeObserver : this._setObserver;
		for (const prop of props)
		{
			if (!dic[prop])
				dic[prop] = [];
			dic[prop].push(watcher);
		}
	}
};

export const Observable = ObservableMixin();
