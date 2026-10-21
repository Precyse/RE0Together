namespace CoopLauncher.Gui;

/// <summary>Tells assistive tools that a painted control is a list and what its items are: each item has a name, a place on
/// the control and a selected state, and can be chosen.</summary>
internal sealed class ItemListAccessibility : Control.ControlAccessibleObject
{
    private readonly Control _control;
    private readonly Func<int> _count;
    private readonly Func<int, string> _name;
    private readonly Func<int, Rectangle> _bounds;
    private readonly Func<int, bool> _selected;
    private readonly Action<int> _choose;

    public ItemListAccessibility(Control owner, Func<int> count, Func<int, string> name, Func<int, Rectangle> bounds,
        Func<int, bool> selected, Action<int> choose) : base(owner)
    {
        _control = owner;
        _count = count;
        _name = name;
        _bounds = bounds;
        _selected = selected;
        _choose = choose;
    }

    public override AccessibleRole Role => AccessibleRole.List;

    public override int GetChildCount() => _count();

    public override AccessibleObject GetChild(int index) => new Item(this, index);

    private sealed class Item : AccessibleObject
    {
        private readonly ItemListAccessibility _list;
        private readonly int _index;

        public Item(ItemListAccessibility list, int index)
        {
            _list = list;
            _index = index;
        }

        public override AccessibleObject Parent => _list;

        public override AccessibleRole Role => AccessibleRole.ListItem;

        public override string? Name
        {
            get => _list._name(_index);
            set { }
        }

        public override Rectangle Bounds => _list._control.RectangleToScreen(_list._bounds(_index));

        public override AccessibleStates State => AccessibleStates.Selectable | AccessibleStates.Focusable
            | (_list._selected(_index) ? AccessibleStates.Selected | AccessibleStates.Focused : AccessibleStates.None);

        public override void Select(AccessibleSelection flags)
        {
            if (_list._control.Enabled && flags.HasFlag(AccessibleSelection.TakeSelection)) _list._choose(_index);
        }
    }
}
