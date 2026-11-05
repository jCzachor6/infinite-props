using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace InfiniteProps
{
    /// <summary>Independent native work and monotonic timeout. Zero work uses
    /// the finite default; zero timeout disables the clock. Cancellation can
    /// arrive on another thread while Generate is running synchronously.</summary>
    public sealed class ExecutionLimits
    {
        public ulong WorkLimit { get; }
        public ulong TimeoutNanoseconds { get; }
        public CancellationToken Cancellation { get; }
        public ExecutionLimits(ulong workLimit = 0, ulong timeoutNanoseconds = 0, CancellationToken cancellation = default)
        { WorkLimit = workLimit; TimeoutNanoseconds = timeoutNanoseconds; Cancellation = cancellation; }
    }

    internal static partial class Native
    {
        [StructLayout(LayoutKind.Sequential)] internal struct Limits
        {
            internal uint Size, Flags;
            internal ulong WorkLimit, TimeoutNanoseconds;
            internal IntPtr Cancellation, Poll, PollContext;
        }
        [StructLayout(LayoutKind.Sequential)] internal struct WorkStats
        { internal uint Size, Flags; internal ulong Budget, Remaining, Used; }
        internal sealed class CancellationHandle : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal CancellationHandle(IntPtr value) : base(true) { SetHandle(value); }
            protected override bool ReleaseHandle() { iprop_cancellation_free(handle); return true; }
        }
        internal sealed class ExecutionScope : IDisposable
        {
            internal Limits Value = Sized<Limits>();
            private readonly CancellationHandle token;
            private CancellationTokenRegistration registration;
            internal ExecutionScope(ExecutionLimits limits, ErrorBuffer error)
            {
                if (limits == null) return;
                Value.WorkLimit = limits.WorkLimit; Value.TimeoutNanoseconds = limits.TimeoutNanoseconds;
                if (!limits.Cancellation.CanBeCanceled) return;
                IntPtr raw;
                error.Check(iprop_cancellation_create(out raw, ref error.Value));
                token = new CancellationHandle(raw);
                try
                {
                    Value.Cancellation = token.DangerousGetHandle();
                    registration = limits.Cancellation.Register(() => iprop_cancellation_cancel(token));
                }
                catch { token.Dispose(); throw; }
            }
            public void Dispose()
            {
                // Wait for an in-flight callback before freeing the C handle.
                registration.Dispose();
                token?.Dispose();
            }
        }
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_generate_controlled(ProgramHandle program, ref Options options, IntPtr parameters, ref Limits limits, out IntPtr result, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_result_query_work(IntPtr result, ref WorkStats work, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern uint iprop_cancellation_create(out IntPtr token, ref Error error);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void iprop_cancellation_cancel(CancellationHandle token);
        [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)] internal static extern void iprop_cancellation_free(IntPtr token);
    }
}
