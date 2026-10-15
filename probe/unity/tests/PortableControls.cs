using System;
using System.IO;
using FrameAppPayloadProbe;

internal static class PortableControls
{
    private static int _checks;
    private static void Require(bool condition, string label)
    {
        _checks++;
        if (!condition) throw new Exception("Control failed: " + label);
    }
    private static void Reject(Action operation, string label)
    {
        bool failed = false;
        try { operation(); } catch (InvalidDataException) { failed = true; }
        Require(failed, label);
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1) throw new Exception("Supply the original local payload directory.");
        for (int mask = 0; mask < 32; mask++)
        {
            var policy = new ProbePolicy();
            bool started = policy.Start(10, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                                        (mask & 8) != 0, (mask & 16) != 0);
            Require(started == (mask == 31), "all five start gates");
        }
        foreach (double invalid in new[] { double.NaN, double.PositiveInfinity, double.NegativeInfinity, -1d })
            Require(!new ProbePolicy().Start(invalid, true, true, true, true, true), "invalid start time");
        for (int mask = 0; mask < 64; mask++)
            foreach (ProbeEye eye in new[] { ProbeEye.Unknown, ProbeEye.Left, ProbeEye.Right, (ProbeEye)99 })
            {
                var policy = new ProbePolicy();
                Require(policy.Start(10, true, true, true, true, true), "prepare valid running policy");
                ProbeEye actual = policy.Select(11, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                    (mask & 8) != 0, (mask & 16) != 0, (mask & 32) != 0, eye);
                ProbeEye expected = mask == 63 && (eye == ProbeEye.Left || eye == ProbeEye.Right) ? eye : ProbeEye.Unknown;
                Require(actual == expected, "camera/XR/both-eye/eye admission matrix");
            }
        foreach (double expired in new[] { 130d, 131d, 9d, double.NaN, double.PositiveInfinity })
        {
            var policy = new ProbePolicy();
            policy.Start(10, true, true, true, true, true);
            Require(!policy.IsRunning(expired), "deadline or invalid clock stops output");
            Require(policy.Phase == ProbePhase.Stopped, "stop is permanent");
            Require(!policy.Start(200, true, true, true, true, true), "no rearm after stop");
        }
        var one = new ProbePolicy();
        Require(one.Phase == ProbePhase.Prepared && !one.IsRunning(99999), "waiting for F8 consumes no run window");
        Require(one.Start(100000, true, true, true, true, true), "F8 start after arbitrary preparation wait");
        Require(one.IsRunning(100119.999), "not expired before original deadline");
        Require(!one.Start(100100, true, true, true, true, true), "second start cannot reset deadline");
        Require(one.StartedAt == 100000, "original time remains immutable");
        one.Stop();
        Require(one.Select(100101, true, true, true, true, true, true, ProbeEye.Left) == ProbeEye.Unknown,
                "manual stop immediately uses source fallback");

        string root = Path.GetFullPath(args[0]);
        Func<string, ProbeManifest> load = eye => ProbeManifest.Load(Path.Combine(root, eye + "-source.json"), eye);
        ProbeManifest left = load("left"), right = load("right");
        ProbeManifest.CheckPair(left, right);
        Require(left.Session == "0123456789abcdef0123456789abcdef" && left.ContentId == 7, "actual trusted fixture source ID");
        Reject(() => ProbeManifest.Load(Path.Combine(root, "right-source.json"), "left"), "wrong-eye source record");
        ProbeManifest bad = load("right"); bad.ContentId++;
        Reject(() => ProbeManifest.CheckPair(left, bad), "cross-generation pair");
        bad = load("right"); bad.Session = "fedcba9876543210fedcba9876543210";
        Reject(() => ProbeManifest.CheckPair(left, bad), "cross-session pair");
        bad = load("left"); bad.SourceExtent[0] = 513;
        Reject(() => bad.Check("left"), "bounded source extent");
        bad = load("left"); bad.Placements.Coverage[0]++;
        Reject(() => bad.Check("left"), "layout offset change");
        bad = load("left"); bad.Convention = "premultiplied";
        Reject(() => bad.Check("left"), "unsupported convention cannot silently convert");
        bad = load("left"); bad.PassthroughAuthorized = true;
        Reject(() => bad.Check("left"), "no activation input");
        bad = load("left"); bad.OpaqueFallbackRequired = false;
        Reject(() => bad.Check("left"), "no missing opaque fallback");
        bad = load("left"); bad.CanvasSha256 = new string('G', 64);
        Reject(() => bad.Check("left"), "strict digest encoding");
        bad = load("left"); bad.HeaderCopies = 1;
        Reject(() => bad.Check("left"), "cannot reduce identity redundancy");
        bad = load("left"); bad.Session = new string('0', 32);
        Reject(() => bad.Check("left"), "zero session nonce");

        string temporary = Path.Combine(Path.GetTempPath(), "frame-app-payload-policy-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temporary);
        try
        {
            string path = Path.Combine(temporary, "oversize.json");
            File.WriteAllBytes(path, new byte[8193]);
            Reject(() => ProbeManifest.ReadRegular(path, 8192), "bounded file input");
            Reject(() => ProbeManifest.ReadRegular("relative.json", 8192), "explicit absolute path");
            Require(ProbeManifest.Sha256(new byte[] { 97, 98, 99 }) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known vector");
        }
        finally { Directory.Delete(temporary, true); }
        Console.WriteLine("{\"schema\":\"frame-app-payload-portable-controls-v1\",\"checks\":" + _checks
            + ",\"passed\":true,\"unity_callbacks_tested\":false,\"native_carriage_proved\":false}");
        return 0;
    }
}
