namespace LostArt.Generator.Core;

/// <summary>Collects warnings and errors so the run can print a summary and fail at the end.</summary>
public sealed class Log
{
    private readonly List<string> _warnings = new();
    private readonly List<string> _errors = new();
    public bool Quiet { get; init; }

    public IReadOnlyList<string> Warnings => _warnings;
    public IReadOnlyList<string> Errors => _errors;

    public void Info(string msg)
    {
        if (!Quiet) Console.WriteLine(msg);
    }

    public void Warn(string msg)
    {
        _warnings.Add(msg);
        if (!Quiet) Console.WriteLine("  [warn] " + msg);
    }

    public void Error(string msg)
    {
        _errors.Add(msg);
        Console.Error.WriteLine("  [error] " + msg);
    }
}

public sealed class GeneratorException(string message) : Exception(message);
