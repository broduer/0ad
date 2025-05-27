export class FSMv2State
{
	constructor(name)
	{
		this.name = name;
	}

	onEnter(previousStateName)
	{
	}

	onLeave(nextStateName)
	{
	}
}

export class FSMv2
{
	constructor(initialState, transitions)
	{
		this.initialState = initialState.name;
		this.currentState = initialState;
		this.states = {};
		this.transitions = transitions;
		this.addState(initialState);
	}

	// Add a state to the FSM
	addState(state)
	{
		if (!(state instanceof FSMv2State))
			throw Error("FSMv2: state must be an instance of FSMv2State");

		this.states[state.name] = state;
	}

	// Check if a transition is possible based on the transition JSON
	canTransition(toStateName)
	{
		if (this.initialState == toStateName)
			return true;
		const validTransitions = this.transitions[this.currentState.name] || [];
		return validTransitions.length > 0 ? validTransitions.includes(toStateName) : true;
	}

	// Transition to another state if the key matches and transition is allowed
	transitionTo(name)
	{
		if (!this.states[name])
		{
			warn(`Cannot transition to ${name}. State not found.`);
			return false;
		}

		if (this.canTransition(name))
		{
			const previousStateName = this.currentState.name;
			this.currentState.onLeave(name);
			this.currentState = this.states[name];
			this.currentState.onEnter(previousStateName);
			return true;
		}

		warn(`Cannot transition to ${name} from ${this.currentState.name}. Invalid key or transition.`);
		return false;
	}
}
