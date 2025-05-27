class Identity
{
	Init()
	{
		this.phenotype = "default";
	}

	GetSelectionGroupName()
	{
		return this.template.SelectionGroupName || "";
	}

	GetPhenotype()
	{
		return this.phenotype;
	}

	GetCiv()
	{
		return this.template.Civ;
	}

	SetName(newName)
	{
		this.name = newName;
	}

	GetName()
	{
		return this.name || this.template.GenericName;
	}

	IsUndeletable()
	{
		return this.template.Undeletable == "true";
	}
}

Identity.prototype.Schema =`
	<a:help>Specifies various names and values associated with the entity, typically for GUI display to users.</a:help>
	<element name='Civ' a:help='Civilization that this unit is primarily associated with, typically a 4-letter code. Choices include: gaia (world objects), skirm (skirmish map placeholders), athen (Athenians), brit (Britons), cart (Carthaginians), gaul (Gauls), iber (Iberians), kush (Kushites), mace (Macedonians), maur (Mauryas), pers (Persians), ptol (Ptolemies), rome (Romans), sele (Seleucids), spart (Spartans).'>
		<text/>
	</element>
	<optional>
		<element name='SelectionGroupName' a:help='Name used to group ranked entities.'>
			<text/>
		</element>
	</optional>
	<element name='GenericName' a:help='Generic English-language name for this entity.'>
		<text/>
	</element>
	<element name='Undeletable' a:help='Prevent players from deleting this entity.'>
		<data type='boolean'/>
	</element>
	<optional>
		<element name='Classes' a:help='Optional list of space-separated classes applying to this entity. Choices include: AfricanElephant, AmunGuard, Animal, ApedemakGuard, Ashoka, Barter, CitizenSoldier, CivCentre, CivSpecific, ConquestCritical, Domestic, DropsiteFood, DropsiteMetal, DropsiteStone, DropsiteWood, FastMoving, FemaleCitizen, Foundation, GarrisonFortress, Human, IndianElephant, Juggernaut, KushTrireme, MercenaryCamp, Organic, Player, PtolemyIV, SeaCreature, Spy, Structure, Unit, WallLong, WallMedium, WallShort, WallTower.'>
			<attribute name='datatype'>
					<value>tokens</value>
				</attribute>
			<text/>
		</element>
	</optional>
`;
Engine.RegisterComponentType(IID_Identity, "Identity", Identity);
