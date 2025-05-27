export class Command
{
	finalize = false;
	execute()
	{
		throw new Error('execute() must be implemented');
	}

	undo()
	{
		throw new Error('undo() must be implemented');
	}

	redo()
	{
		throw new Error('redo() must be implemented');
	}

	mergeWith(command)
	{
		throw new Error('mergeWith() must be implemented');
	}

	mergeableWith(command)
	{
		if (this.getType() !== command.getType())
			return false;

		if (this.finalize || command.finalize)
			return false;

		return true;
	}

	getType()
	{
		throw new Error('getType() must be implemented');
	}
}

export class CommandProc
{
	historyCommand = [];
	redoCommand = [];

	submit(command)
	{
		if (!(command instanceof Command))
			throw new Error('Invalid command');

		command.execute();
		this.redoCommand = [];

		if (this.historyCommand.length > 0)
		{
			const lastCommand = this.historyCommand[this.historyCommand.length - 1];
			if (lastCommand.mergeableWith(command))
			{
				command.mergeWith(lastCommand);
				return;
			}
		}

		this.historyCommand.push(command);
	}

	flush()
	{
		this.historyCommand = [];
		this.redoCommand = [];
	}

	undo()
	{
		if (this.historyCommand.length === 0)
			return;

		const command = this.historyCommand.pop();
		command.undo();
		this.redoCommand.push(command);
	}

	redo()
	{
		if (this.redoCommand.length === 0)
			return;

		const command = this.redoCommand.pop();
		command.redo();
		this.historyCommand.push(command);
	}
}
