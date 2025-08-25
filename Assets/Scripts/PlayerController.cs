using UnityEngine;

public class PlayerController : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float laneOffset = 2.0f; // расстояние между дорожками
    [SerializeField] private float moveSpeed = 10.0f; // скорость плавного смещения по горизонтали
    [SerializeField] private int currentLaneIndex = 1; // 0-левая, 1-центральная, 2-правая

    [Header("Swipe")]
    [SerializeField] private float minSwipeDistance = 50f; // в пикселях

    [Header("Attack")]
    [SerializeField] private float attackDuration = 0.2f;
    [SerializeField] private GameObject attackHitbox; // дочерний объект с коллайдером (isTrigger=true)

    private Vector2 touchStartPos;
    private bool isAttacking = false;

    private void Start()
    {
        // Отключаем хитбокс атаки при старте
        if (attackHitbox != null)
        {
            attackHitbox.SetActive(false);
        }
    }

    private void Update()
    {
        HandleSwipeInput();
        MoveToTargetLane();
    }

    private void HandleSwipeInput()
    {
        if (Input.touchCount > 0)
        {
            Touch touch = Input.GetTouch(0);
            if (touch.phase == TouchPhase.Began)
            {
                touchStartPos = touch.position;
            }
            else if (touch.phase == TouchPhase.Ended || touch.phase == TouchPhase.Canceled)
            {
                Vector2 delta = touch.position - touchStartPos;
                if (delta.magnitude < minSwipeDistance) return;

                bool isHorizontal = Mathf.Abs(delta.x) > Mathf.Abs(delta.y);
                if (isHorizontal)
                {
                    if (delta.x > 0)
                    {
                        MoveRight();
                    }
                    else
                    {
                        MoveLeft();
                    }
                }
                else
                {
                    if (delta.y > 0)
                    {
                        TriggerAttack();
                    }
                }
            }
        }

#if UNITY_EDITOR || UNITY_STANDALONE
        // Ввод в редакторе/на ПК для тестов
        if (Input.GetKeyDown(KeyCode.LeftArrow)) MoveLeft();
        if (Input.GetKeyDown(KeyCode.RightArrow)) MoveRight();
        if (Input.GetKeyDown(KeyCode.UpArrow)) TriggerAttack();
#endif
    }

    private void MoveLeft()
    {
        currentLaneIndex = Mathf.Clamp(currentLaneIndex - 1, 0, 2);
    }

    private void MoveRight()
    {
        currentLaneIndex = Mathf.Clamp(currentLaneIndex + 1, 0, 2);
    }

    private void MoveToTargetLane()
    {
        float targetX = (currentLaneIndex - 1) * laneOffset; // середина — 0
        Vector3 targetPos = new Vector3(targetX, transform.position.y, transform.position.z);
        transform.position = Vector3.Lerp(transform.position, targetPos, Time.deltaTime * moveSpeed);
    }

    private void TriggerAttack()
    {
        if (isAttacking || attackHitbox == null) return;
        isAttacking = true;
        attackHitbox.SetActive(true);
        Invoke(nameof(EndAttack), attackDuration);
    }

    private void EndAttack()
    {
        if (attackHitbox != null)
        {
            attackHitbox.SetActive(false);
        }
        isAttacking = false;
    }

    private void OnTriggerEnter(Collider other)
    {
        // Если что-то напрямую сталкивается с игроком (например, самокат) — конец игры
        if (other.CompareTag("Scooter"))
        {
            ScoreManager.Instance?.TriggerGameOver();
        }
    }
}

