namespace Yule.Server.Admin;

/// <summary>
/// What happened when the administrator asked for a change: either it was refused with a message per field (and
/// nothing was changed), or it was saved and the broker was updated (or will be, see <see cref="ApplyOutcome"/>).
/// </summary>
public sealed record ChangeResult(IReadOnlyDictionary<string, string> Errors, ApplyOutcome Broker)
{
    public bool Succeeded => Errors.Count == 0;

    /// <summary>Whether the target of the change was missing, as opposed to the input being unacceptable.</summary>
    public bool NotFound { get; init; }

    public static ChangeResult Saved(ApplyOutcome broker) => new(new Dictionary<string, string>(), broker);

    public static ChangeResult Refused(string field, string message) =>
        new(new Dictionary<string, string> { [field] = message }, ApplyOutcome.Done);

    public static ChangeResult Refused(IReadOnlyDictionary<string, string> errors) => new(errors, ApplyOutcome.Done);

    public static ChangeResult Missing(string what) =>
        new(new Dictionary<string, string> { [""] = $"That {what} no longer exists." }, ApplyOutcome.Done) { NotFound = true };
}
