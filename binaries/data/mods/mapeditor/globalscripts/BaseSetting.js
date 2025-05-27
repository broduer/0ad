/**
 * Observable is a self-proxy to enable opt-in reactive programming.
 * Properties of an Observable can be watched, and whenever they are changed
 * a callback will be fired.
 * We are using the same approach from gui/common/Objservable.js
 */
var ObservableSettingMixin = (Parent) => class ObservableSetting extends (() => Parent || Object)()
{
	constructor()
	{
		super();

		// Stores observers that are fired when the value is changed
		Object.defineProperty(this, "_changeObserver", {
			"value": {},
			"enumerable": false,
		});
		// Stores observers that are fired when the value is assigned to,
		// even if it isn't changed.
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
var ObservableSetting = ObservableSettingMixin();


/**
 * The Base settings are split in subclasses that are only responsible
 * for a logical subset of settings.
 * These are observables so updates are automated.
 */
class BaseSetting extends ObservableSetting /* ProfilableMixin(Observable) /* Replace to profile automatically. */
{
	constructor(settings)
	{
		super();
		this.settings = settings;
	}

	toInitAttributes(attribs)
	{
		warn(`toInitAttributes not defined ${this.constructor.name}`);
	}

	fromInitAttributes(attribs)
	{
		warn(`fromInitAttributes not defined ${this.constructor.name}`);
	}
}
