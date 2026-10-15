using System;

namespace FrameAppPayloadProbe
{
    public enum ProbeEye { Unknown, Left, Right }
    public enum ProbePhase { Prepared, Running, Stopped }

    // Pure admission/lifecycle code shared with portable controls, not an XR implementation.
    public sealed class ProbePolicy
    {
        public const string HeadCameraName = "GloomhavenVR.HeadCamera";
        public const double MaximumSeconds = 120;
        public ProbePhase Phase { get; private set; } = ProbePhase.Prepared;
        public double StartedAt { get; private set; }

        public bool Start(double now, bool uniqueNamedCamera, bool builtIn, bool xrActive,
                          bool multiPass, bool bothEyes)
        {
            if (Phase != ProbePhase.Prepared || !Finite(now) || now < 0 || !uniqueNamedCamera
                || !builtIn || !xrActive || !multiPass || !bothEyes)
                return false;
            StartedAt = now;
            Phase = ProbePhase.Running;
            return true;
        }

        public void Stop() { Phase = ProbePhase.Stopped; }

        public bool IsRunning(double now)
        {
            if (Phase != ProbePhase.Running)
                return false;
            if (!Finite(now) || now < StartedAt || now - StartedAt >= MaximumSeconds)
            {
                Stop();
                return false;
            }
            return true;
        }

        public ProbeEye Select(double now, bool sameAttachedCamera, bool currentCameraMatches,
                               bool builtIn, bool xrActive, bool multiPass, bool bothEyes, ProbeEye eye)
        {
            if (!IsRunning(now) || !sameAttachedCamera || !currentCameraMatches || !builtIn
                || !xrActive || !multiPass || !bothEyes)
                return ProbeEye.Unknown;
            return eye == ProbeEye.Left || eye == ProbeEye.Right ? eye : ProbeEye.Unknown;
        }

        private static bool Finite(double value) { return !double.IsNaN(value) && !double.IsInfinity(value); }
    }
}
