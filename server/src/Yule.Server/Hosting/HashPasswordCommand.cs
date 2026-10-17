using Yule.Server.Security;

namespace Yule.Server.Hosting;

/// <summary>
/// <c>hash-password</c>: reads a password (hidden prompt, or one line from standard input) and prints the hash to
/// put in <c>Yule__Admin__PasswordHash</c>.
/// </summary>
public static class HashPasswordCommand
{
    public static int Run(TextReader input, TextWriter output, TextWriter error, bool interactive)
    {
        string? password;
        if (interactive)
        {
            output.Write("New admin password: ");
            password = ReadHidden();
            output.WriteLine();
            output.Write("Repeat password: ");
            var again = ReadHidden();
            output.WriteLine();
            if (password != again)
            {
                error.WriteLine("The passwords do not match.");
                return 1;
            }
        }
        else
        {
            password = input.ReadLine();
        }

        if (string.IsNullOrEmpty(password) || password.Length < 12)
        {
            error.WriteLine("Use a password of at least 12 characters.");
            return 1;
        }

        output.WriteLine(AdminPasswordHasher.Hash(password));
        return 0;
    }

    private static string ReadHidden()
    {
        var chars = new List<char>();
        while (true)
        {
            var key = Console.ReadKey(intercept: true);
            if (key.Key == ConsoleKey.Enter)
            {
                return new string(chars.ToArray());
            }

            if (key.Key == ConsoleKey.Backspace)
            {
                if (chars.Count > 0)
                {
                    chars.RemoveAt(chars.Count - 1);
                }
            }
            else if (!char.IsControl(key.KeyChar))
            {
                chars.Add(key.KeyChar);
            }
        }
    }
}
