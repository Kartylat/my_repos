using UnityEngine;
using UnityEngine.Events;

public class ScoreManager : MonoBehaviour
{
    public static ScoreManager Instance { get; private set; }

    [SerializeField]
    private int score = 0;

    public UnityEvent<int> OnScoreChanged = new UnityEvent<int>();
    public UnityEvent OnGameOver = new UnityEvent();

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);
    }

    public int CurrentScore => score;

    public void AddScore(int amount)
    {
        if (amount <= 0) return;
        score += amount;
        OnScoreChanged.Invoke(score);
    }

    public void ResetScore()
    {
        score = 0;
        OnScoreChanged.Invoke(score);
    }

    public void TriggerGameOver()
    {
        OnGameOver.Invoke();
        // Additional handling can be added by listeners (UI reload, scene reset, etc.)
    }
}

